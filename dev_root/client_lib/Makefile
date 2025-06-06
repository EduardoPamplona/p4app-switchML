#  Copyright 2021 Intel-KAUST-Microsoft
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

# SwitchML Project
# @file client_lib makefile
# @brief The makefile to compile the client library.
#
# Build Variables --
# Format: VARIABLE (type: default value): usage
#
# - DEBUG (boolean: 0): Disable optimizations, add debug symbols, and enable detailed debugging messages.
# - DPDK (boolean: 0): Compile and include the dpdk backend.
# - RDMA (boolean: 0): Compile and include the rdma backend.
# - DUMMY (boolean: 1): Compile and include the dummy backend.
# - VCL (boolean: 1): Compile with the vector class library (Used to speedup quantization on the CPU)
# - TIMEOUTS (boolean: 1): Compile with timeouts and retransmissions support.
# - BUILDDIR (path: dev_root/build): Where to store generated objects/include files/libraries/binaries...etc.
# - GRPC_HOME (path: dev_root/third_party/grpc/build): Where to look for the GRPC installation
# - DPDK_HOME (path: dev_root/third_party/dpdk/build): Where to look for the DPDK installation
# - DPDK_SDK (path: dev_root/third_party/dpdk): Where to look for the DPDK SDK.
# - VCL_HOME (path: dev_root/third_party/vcl): Where to look for VCL headers
#
# Note: we postfix most variables with _ to avoid collisions with DPDK's build system

# Init path variables
DEVROOT := $(dir $(realpath $(firstword $(MAKEFILE_LIST))))..
BUILDDIR ?= $(DEVROOT)/build

INCDIR_ := $(BUILDDIR)/include/switchml
LIBDIR_ := $(BUILDDIR)/lib
OBJDIR_ := $(BUILDDIR)/obj
SRCDIR_ ?= $(DEVROOT)/client_lib/src
SRCDIRS_ := $(SRCDIR_) $(SRCDIR_)/prepostprocessors $(SRCDIR_)/schedulers # The directories that we include by default.
CFGFILES := $(SRCDIR_)/configs/general.cfg
LIB ?= libswitchml-client.a # Name constraint from DPDK

ifeq ($(DPDK),1)
DPDK_HOME ?= $(DEVROOT)/third_party/dpdk/build
DPDK_SDK ?= $(DEVROOT)/third_party/dpdk
RTE_SDK := $(DPDK_SDK)
RTE_TARGET = build
include $(RTE_SDK)/mk/rte.vars.mk
endif

# Compiler / linker flags
CXXFLAGS += -std=c++17 -fPIC
LDFLAGS += -lboost_program_options -lglog -lpthread -lstdc++

# Parse compilation options -----

ifeq ($(DEBUG),1)
$(info DEBUG is set. Enabling all compiler warnings, adding debug symbols, and disabling optimizations.)
CXXFLAGS += -DDEBUG -Wall -Wextra -g -O0
else
$(info DEBUG is not set. Enabling all optimizations.)
CXXFLAGS += -DNDEBUG -O3 
endif

ifneq ($(TIMEOUTS),0)
$(info TIMEOUTS set. Compiling with retransmissions)
CXXFLAGS += -DTIMEOUTS
CFGFILES += $(SRCDIR_)/configs/timeouts.cfg
endif

ifeq ($(DUMMY),0)
ifneq ($(DPDK),1)
ifneq ($(RDMA),1)
ifneq ($(UDP),1)
$(error You must enable at least one backend)
endif
endif
endif
endif

ifeq ($(RDMA),1)
ifeq ($(DPDK),1)
$(error Enabling both DPDK and RDMA backends is not supported.)
endif
endif

ifneq ($(DUMMY),0)
$(info DUMMY set. Compiling the dummy backend..)
CXXFLAGS += -DDUMMY
SRCDIRS_ += $(SRCDIR_)/backends/dummy
CFGFILES += $(SRCDIR_)/configs/dummy.cfg
endif

ifeq ($(UDP),1)
$(info UDP set. Compiling the UDP backend..)
GRPC = 1
CXXFLAGS += -DUDP
SRCDIRS_ += $(SRCDIR_)/backends/udp
CFGFILES += $(SRCDIR_)/configs/udp.cfg
endif

ifeq ($(DPDK),1)
$(info DPDK set. Compiling the dpdk backend..)
GRPC = 1
CXXFLAGS += -DDPDK
SRCDIRS_ += $(SRCDIR_)/backends/dpdk
CFGFILES += $(SRCDIR_)/configs/dpdk.cfg
endif

ifeq ($(RDMA),1)
$(info RDMA set. Compiling the rdma backend..)
GRPC = 1
CXXFLAGS += -DRDMA
SRCDIRS_ += $(SRCDIR_)/backends/rdma
CFGFILES += $(SRCDIR_)/configs/rdma.cfg
endif

ifneq ($(VCL), 0)
$(info VCL set. Compiling with vector instructions support)
VCL_HOME ?= $(DEVROOT)/third_party/vcl
CXXFLAGS += -DVCL -I $(VCL_HOME)
ifeq ($(DEBUG), 1)
$(warning VCL may cause problems if it was compiled with the DEBUG flag and without optimizations )
endif
endif

ifeq ($(GRPC),1)
GRPC_HOME ?= $(DEVROOT)/third_party/grpc/build
PROTOS=protos
CXXFLAGS += -I $(GRPC_HOME)/include
# They might not be generated so we should add them explicitly just in case
SRCS-absolute_ += $(SRCDIR_)/switchml.pb.cc $(SRCDIR_)/switchml.grpc.pb.cc
endif

OBJDIRS_ := $(SRCDIRS_:$(SRCDIR_)%=$(OBJDIR_)%)
CXXFLAGS += $(shell printf '-I %s ' $(SRCDIRS_)) # add includes

# Source files
SRCS-absolute_ += $(shell find $(SRCDIRS_) -maxdepth 1 -name "*.cc" )
ifneq ($(GRPC),1)
SRCS-absolute_ := $(filter-out %grpc_client.cc,$(SRCS-absolute_))
endif
SRCS_ := $(shell realpath --relative-to="$(SRCDIR_)" $(SRCS-absolute_))
SRCS-y := $(SRCS-absolute_) # Name constraint from DPDK

# Header files
HDRS-absolute_ := $(shell find $(SRCDIRS_) -maxdepth 1 -name "*.h" )
ifneq ($(GRPC),1)
HDRS-absolute_ := $(filter-out %grpc_client.h,$(HDRS-absolute_))
endif
HDRS_ := $(shell realpath --relative-to="$(SRCDIR_)" $(HDRS-absolute_))

# Object files
OBJS_ += $(SRCS_:%.cc=$(OBJDIR_)/%.o)

# Version info
# This information is to be included in the compiled library
# and printed when the context is created
GIT_REMOTE=$(shell git remote get-url --push origin)
GIT_HASH=$(shell git rev-parse HEAD)
GIT_BRANCH=$(shell git branch | grep "^\*" | sed 's/^..//')
GIT_STATUS=$(shell git status --short)
VERSION_INFO := '"CXXFLAGS=$(CXXFLAGS) GIT_REMOTE=$(GIT_REMOTE) GIT_BRANCH=$(GIT_BRANCH) GIT_HASH=$(GIT_HASH) GIT_STATUS=$(GIT_STATUS)"'
CXXFLAGS += -DVERSION_INFO=$(VERSION_INFO)

# Targets

.PHONY: default_
ifeq ($(DPDK), 1)
# With DPDK the DPDK Makefiles are the ones that actually handle the compilation
# of our source files. So we do not need to compile them ourselves.
# Though it does use the standard variable names such as CXXFLAGS and LDFLAGS when 
# it compiles our sources
default_: $(PROTOS) _postbuild $(INCDIR_) $(LIBDIR_) config_
	$(Q)$(MAKE) clean
	cp $(RTE_OUTPUT)/$(LIB) $(LIBDIR_)/
	$(RM) -r $(RTE_OUTPUT)
	$(RM) _postclean
else
# If we're not using DPDK then we need to compile the library ourselves.
default_: $(PROTOS) $(LIBDIR_)/$(LIB) $(INCDIR_) config_
endif
# Copy headers
	cd $(SRCDIR_) && cp --parents $(HDRS_) $(INCDIR_)

$(LIBDIR_)/$(LIB): $(OBJS_) $(LIBDIR_) 
	$(AR) rcs $(LIBDIR_)/$(LIB) $(OBJS_)

$(OBJDIR_)/%.o: $(SRCDIR_)/%.cc $(OBJDIRS_)
	$(CXX) $(CXXFLAGS) -c $< -o $@ $(LDFLAGS)

.PHONY: config_
config_:
	cat $(CFGFILES) > $(BUILDDIR)/switchml.cfg

.PHONY: protos
protos:
	$(MAKE) -C $(DEVROOT)/protos cpp BUILDDIR=$(SRCDIR_)

$(OBJDIRS_):
	mkdir -p $@

$(LIBDIR_):
	mkdir -p $(LIBDIR_)

$(INCDIR_):
	mkdir -p $(INCDIR_)

ifeq ($(DPDK), 1)
include $(RTE_SDK)/mk/rte.extlib.mk
else
# DPDK has its own clean target that we use 
.phony: clean
clean:
	$(RM) -r $(OBJDIR_) 
	$(RM) -r $(INCDIR_)
	$(RM) $(LIBDIR_)/$(LIB)
	$(MAKE) -C $(DEVROOT)/protos clean BUILDDIR=$(SRCDIR_)
endif
