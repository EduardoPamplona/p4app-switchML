/*
  Copyright 2021 Intel-KAUST-Microsoft

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

/**
 * SwitchML Project
 * @file udp_backend.cc
 * @brief Implements the UdpBackend class.
 */

#include "udp_backend.h"
#include "udp_worker_thread.h"
#include "common_cc.h"

namespace switchml {

UdpBackend::UdpBackend(Context& context, Config& config)
    : Backend(context, config),
      grpc_client_(config) {
    // Allocate pending packets array
    pending_packets_ = new std::vector<UdpPacket>[config.general_.num_worker_threads];
}

UdpBackend::~UdpBackend() {
    // Free the pending packets array
    delete[] pending_packets_;
}

void UdpBackend::SetupWorker() {
    VLOG(0) << "Setting up UDP worker.";

    // Parse worker addresses from config
    struct in_addr w_ip;
    LOG_IF(FATAL, inet_aton(this->config_.backend_.udp.worker_ip_str.c_str(), &w_ip) == 0)
        << "Failed to parse ip address '" << this->config_.backend_.udp.worker_ip_str << "'.";
    this->worker_endpoint_addr_.ip = w_ip.s_addr;
    this->worker_endpoint_addr_.port = htons(this->config_.backend_.udp.worker_port);
    
    // Setup switch through controller
    this->SetupSwitch();
    
    // Create worker threads
    for(int i = 0; i < this->config_.general_.num_worker_threads; i++) {
        this->worker_threads_.emplace_back(this->context_, *this, this->config_, i);
        this->worker_threads_[i].Start();
    }
}

void UdpBackend::SetupSwitch() {
    // Generate a session ID
    uint64_t session_id = 0;
    
    if (this->config_.general_.rank == 0) {
        auto current_time = std::chrono::high_resolution_clock::now();
        auto time_since_epoch = current_time.time_since_epoch();
        auto nanoseconds_since_epoch =
            std::chrono::duration_cast<std::chrono::nanoseconds>(time_since_epoch);
        session_id = nanoseconds_since_epoch.count();
        DVLOG(1) << "Session id is 0x" << std::hex << session_id << std::dec;
    }
    
    // Broadcast session ID to other workers
    switchml_proto::BroadcastRequest bcast_request;
    switchml_proto::BroadcastResponse bcast_response;
    bcast_request.set_value(session_id);
    bcast_request.set_rank(this->config_.general_.rank);
    bcast_request.set_num_workers(this->config_.general_.num_workers);
    bcast_request.set_root(0);
    
    this->grpc_client_.Broadcast(bcast_request, &bcast_response);
    
    session_id = bcast_response.value();
    
    // Send connection request to controller
    switchml_proto::UdpSessionRequest request;
    switchml_proto::UdpSessionResponse response;
    request.set_session_id(session_id);
    request.set_rank(this->config_.general_.rank);
    request.set_num_workers(this->config_.general_.num_workers);
    request.set_ipv4(ntohl(this->worker_endpoint_addr_.ip));
    request.set_port(ntohs(this->worker_endpoint_addr_.port));
    
    // Set packet size
    uint8_t pkt_len_enum;
    if (this->config_.general_.packet_numel < 64) {
        pkt_len_enum = 0;
    } else if (this->config_.general_.packet_numel < 128) {
        pkt_len_enum = 1;
    } else if (this->config_.general_.packet_numel < 256) {
        pkt_len_enum = 2;
    } else {
        pkt_len_enum = 3;
    }
    request.set_packet_size(switchml_proto::PacketSize(pkt_len_enum));
    
    // Barrier for synchronization
    switchml_proto::BarrierRequest barrier_request;
    switchml_proto::BarrierResponse barrier_response;
    barrier_request.set_num_workers(this->config_.general_.num_workers);
    
    if (this->config_.general_.rank == 0) {
        // First worker clears switch state
        this->grpc_client_.CreateUdpSession(request, &response);
        this->grpc_client_.Barrier(barrier_request, &barrier_response);
    } else {
        // Other workers wait and then process
        this->grpc_client_.Barrier(barrier_request, &barrier_response);
        this->grpc_client_.CreateUdpSession(request, &response);
    }
    
    // Save switch address info from response
    this->switch_endpoint_addr_.ip = htonl(response.ipv4());
    this->switch_endpoint_addr_.port = htons(response.port());
    
    // Final barrier
    this->grpc_client_.Barrier(barrier_request, &barrier_response);
}

void UdpBackend::CleanupWorker() {
    for (auto& worker : this->worker_threads_) {
        worker.Join();
    }
}

struct UdpBackend::EndpointAddress& UdpBackend::GetSwitchEndpointAddr() {
    return this->switch_endpoint_addr_;
}

struct UdpBackend::EndpointAddress& UdpBackend::GetWorkerEndpointAddr() {
    return this->worker_endpoint_addr_;
}

std::vector<UdpWorkerThread>& UdpBackend::GetWorkerThreads() {
    return this->worker_threads_;
}

void UdpBackend::SendBurst(WorkerTid worker_thread_id, const std::vector<UdpPacket>& packets) {
    // Store packets for later retrieval
    for (const auto& packet : packets) {
        this->pending_packets_[worker_thread_id].push_back(packet);
    }
}

void UdpBackend::ReceiveBurst(WorkerTid worker_thread_id, std::vector<UdpPacket>& packets_received) {
    // Get a reference to the pending packets for this worker thread
    auto& pending = this->pending_packets_[worker_thread_id];
    
    // Check if there are any pending packets
    if (pending.empty()) {
        return;
    }
    
    // When process_packets is true, we're in simulation mode
    // In this mode, we simulate what the switch would do by processing locally
    if (this->config_.backend_.udp.process_packets) {
        // Move all pending packets to packets_received
        packets_received = std::move(pending);
        pending.clear();
        
        // Simulate switch aggregation by multiplying each value by the number of workers
        for (auto& packet : packets_received) {
            // Only process if the packet contains int32 values
            if (packet.data_type == DataType::INT32) {
                auto* elements = static_cast<UdpPacketElement*>(packet.entries_ptr);
                for (Numel i = 0; i < packet.numel; i++) {
                    elements[i] *= this->config_.general_.num_workers;
                }
            }
        }
        DVLOG(2) << "Simulated switch aggregation (process_packets=true)";
    } else {
        // When process_packets is false, we're using real switch aggregation
        // In this case, the UdpWorkerThread handles direct communication with the switch
        // We don't need to process anything here as the real switch has already done the aggregation
        packets_received = std::move(pending);
        pending.clear();
        DVLOG(2) << "Using switch-aggregated values (process_packets=false)";
    }
}

} // namespace switchml
