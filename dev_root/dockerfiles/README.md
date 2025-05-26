# Dockerfiles

The dockerfiles directory contains dockerfiles to build switchml with different backends and integrated into different frameworks.

Even if you do not want to use docker, this is a good place to get a step by step guide on how to setup the environment, compile switchml, and integrate into frameworks.

## Available Dockerfiles

- **dummy.dockerfile**: Builds SwitchML with the dummy backend (for testing only)
- **dpdk.dockerfile**: Builds SwitchML with the DPDK backend 
- **rdma.dockerfile**: Builds SwitchML with the RDMA backend
- **udp.dockerfile**: Builds SwitchML with the UDP backend (for local development)
- **dpdk_pytorch.dockerfile**: Builds PyTorch with SwitchML DPDK backend integration
- **rdma_pytorch.dockerfile**: Builds PyTorch with SwitchML RDMA backend integration
- **udp_pytorch.dockerfile**: Builds PyTorch with SwitchML UDP backend integration
- **controller.dockerfile**: Builds the controller application

For more information about the UDP backend, see [UDP_BACKEND_README.md](UDP_BACKEND_README.md)