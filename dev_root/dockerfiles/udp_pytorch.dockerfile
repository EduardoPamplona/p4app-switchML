# SwitchML Project
# @file udp_pytorch.dockerfile
# @brief Generates an image that can be used to run PyTorch with SwitchML UDP backend integrated.
#
# A typical command to start the container is:
# docker run -it --net=host --name switchml-udp-pytorch <name_of_the_image_created_from_this_file>
#

FROM nvidia/cuda:11.2.2-devel-ubuntu20.04

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
    software-properties-common \
    python3 \
    python3-dev \
    python3-pip

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
    pkg-config \
    autoconf \
    libtool \
    curl \
    unzip

# Install PyTorch requirements
RUN pip3 install \
    numpy \
    pytest \
    pyyaml \
    mkl \
    mkl-include \
    setuptools \
    cmake \
    cffi \
    typing_extensions \
    ninja

# Set working directory
WORKDIR /home

# Clone SwitchML repository
ARG SWITCHML_UPDATED
RUN git clone https://github.com/p4lang/p4app-switchML.git /home/switchml

# Build gRPC from source (required for the controller communication)
WORKDIR /home/switchml/dev_root
RUN git submodule update --init -- third_party/grpc && \
    make -C third_party grpc

# Build the client library with UDP backend
RUN git submodule update --init -- third_party/vcl && \
    make DUMMY=0 UDP=1 TIMEOUTS=${TIMEOUTS} VCL=${VCL} DEBUG=${DEBUG}

# Clone PyTorch
WORKDIR /home
RUN git clone --recursive https://github.com/pytorch/pytorch.git && \
    cd pytorch && git checkout v1.9.0

# Apply SwitchML patch to PyTorch
RUN cd /home/pytorch && \
    git apply /home/switchml/dev_root/frameworks_integration/pytorch_patch/switchml.patch && \
    cd third_party/gloo && git apply /home/switchml/dev_root/frameworks_integration/pytorch_patch/gloo.patch

# Build PyTorch with SwitchML integrated
WORKDIR /home/pytorch
ENV CMAKE_PREFIX_PATH=/home/switchml/dev_root/build
RUN python3 setup.py install

# Create example script using SwitchML UDP
RUN echo '#!/bin/bash\n\
# Configure SwitchML to use UDP backend\n\
export SWITCHML_BACKEND="udp"\n\
export SWITCHML_WORKER_IP="${WORKER_IP:-127.0.0.1}"\n\
export SWITCHML_WORKER_PORT="48865"\n\
export SWITCHML_WORKER_ID="${WORKER_ID:-0}"\n\
export SWITCHML_NUM_WORKERS="${NUM_WORKERS:-4}"\n\
export SWITCHML_CONTROLLER_IP="${CONTROLLER_IP:-127.0.0.1}"\n\
export SWITCHML_CONTROLLER_PORT="50051"\n\
\n\
# Enable SwitchML in PyTorch Gloo backend\n\
export USE_SWITCHML_FOR_GLOO="1"\n\
\n\
# Run the provided command with these settings\n\
exec "$@"\n'\
> /usr/local/bin/with_switchml_udp.sh && \
chmod +x /usr/local/bin/with_switchml_udp.sh

# Create a simple example script
RUN echo 'import os\n\
import torch\n\
import torch.distributed as dist\n\
\n\
def run():\n\
    # Initialize the process group with gloo backend\n\
    # The patched gloo will use SwitchML UDP for AllReduce\n\
    dist.init_process_group(\n\
        backend="gloo",\n\
        init_method=f"tcp://{os.environ.get(\"MASTER_ADDR\", \"127.0.0.1\")}:{os.environ.get(\"MASTER_PORT\", \"12345\")}", \n\
        rank=int(os.environ.get(\"RANK\", 0)),\n\
        world_size=int(os.environ.get(\"WORLD_SIZE\", 1))\n\
    )\n\
    \n\
    # Create a tensor to be all-reduced\n\
    tensor = torch.ones(1024, device="cpu") * int(os.environ.get("RANK", 0))\n\
    \n\
    print(f"Rank {os.environ.get(\"RANK\", 0)} before all-reduce: {tensor[0].item()}")\n\
    \n\
    # This will use SwitchML UDP backend\n\
    dist.all_reduce(tensor)\n\
    \n\
    expected = sum(range(int(os.environ.get("WORLD_SIZE", 1))))\n\
    print(f"Rank {os.environ.get(\"RANK\", 0)} after all-reduce: {tensor[0].item()}, expected: {expected}")\n\
    \n\
    # Clean up\n\
    dist.destroy_process_group()\n\
\n\
if __name__ == "__main__":\n\
    run()\n'\
> /home/example_switchml_udp.py

# Set the working directory to the root of the project
WORKDIR /home/switchml

# Default command when container starts
CMD ["/bin/bash"]
