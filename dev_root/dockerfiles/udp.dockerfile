# SwitchML Project
# @file udp.dockerfile
# @brief Generates an image that can be used to run switchml's benchmarks with the UDP backend.
#
# A typical command to start the container is:
# docker run -it --net=host --name switchml-udp <name_of_the_image_created_from_this_file>
#

FROM ubuntu:20.04

# Set default shell to /bin/bash
SHELL ["/bin/bash", "-cu"]

# Set environment variables to avoid interactive prompts during installation
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

ARG TIMEOUTS=1
ARG VCL=1
ARG DEBUG=0

# General packages needed
RUN apt-get update && \
    apt install -y \
    git \
    wget \
    vim \
    nano \
    build-essential \
    net-tools \
    sudo \
    gpg \ 
    lsb-release \
    software-properties-common

# Add kitware's APT repository for cmake
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | \
    gpg --dearmor - | \
    tee /etc/apt/trusted.gpg.d/kitware.gpg >/dev/null && \
    apt-add-repository "deb https://apt.kitware.com/ubuntu/ $(lsb_release -cs) main" && \
    apt update

# Install client library requirements
RUN apt install -y \
    gcc \
    g++ \
    make \
    cmake \
    libboost-program-options-dev \
    libgoogle-glog-dev \
    libnuma-dev \
    pkg-config

# Install gRPC dependencies
RUN apt install -y \
    libssl-dev \
    autoconf \
    libtool \
    curl \
    unzip

# Set working directory
WORKDIR /home

# Clone the switchml repository
ARG SWITCHML_UPDATED
RUN git clone https://github.com/EduardoPamplona/p4app-switchML.git /home/switchml

# Build gRPC from source (required for the controller communication)
WORKDIR /home/switchml/dev_root
RUN git submodule update --init -- third_party/grpc && \
    make -C third_party grpc

# Build the client library with UDP backend
RUN git submodule update --init -- third_party/vcl && \
    make DUMMY=0 UDP=1 TIMEOUTS=${TIMEOUTS} VCL=${VCL} DEBUG=${DEBUG}

# Set the working directory to the root of the project
WORKDIR /home/switchml

# At this point, the client library and benchmarks can be run with the UDP backend.
# Default command when container starts
CMD ["/bin/bash"]
