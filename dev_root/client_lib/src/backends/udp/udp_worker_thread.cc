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
 * @file udp_worker_thread.cc
 * @brief Implements the UdpWorkerThread class.
 */

#include "udp_worker_thread.h"
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "common_cc.h"
#include "prepostprocessor.h"
#include "utils.h"

namespace switchml {

UdpWorkerThread::UdpWorkerThread(Context& context, UdpBackend& backend, Config& config, int thread_id)
    : tid_(thread_id),
      context_(context),
      backend_(backend),
      config_(config),
      socket_fd_(-1),
      thread_(nullptr),
      running_(false),
      port_(config.backend_.udp.worker_port + thread_id) {
    // Create pre/post processor
    ppp_ = PrePostProcessor::CreateInstance(config);
}

UdpWorkerThread::~UdpWorkerThread() {
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

void UdpWorkerThread::Start() {
    running_ = true;
    thread_ = std::make_unique<std::thread>(*this);
}

void UdpWorkerThread::Join() {
    running_ = false;
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
}

void UdpWorkerThread::operator()() {
    VLOG(0) << "UDP worker thread " << tid_ << " started";
    
    // Initialize socket
    if (!InitializeSocket()) {
        LOG(ERROR) << "UDP worker thread " << tid_ << " failed to initialize socket";
        return;
    }
    
    // Main worker loop
    while (running_) {
        Job* job = nullptr;
        
        // Try to get a job from the context
        if (context_.GetJob(&job, tid_)) {
            // Process the job
            ProcessJob(job);
            
            // Mark job as complete
            job->MarkAsComplete();
            DVLOG(1) << "UDP worker thread " << tid_ << " completed job " << job->GetId();
        } else {
            // No job available, sleep for a bit
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    
    VLOG(0) << "UDP worker thread " << tid_ << " exiting";
}

bool UdpWorkerThread::InitializeSocket() {
    // Create UDP socket
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) {
        LOG(ERROR) << "Failed to create socket: " << strerror(errno);
        return false;
    }
    
    // Set socket options
    int opt_val = 1;
    setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val));
    
    // Configure worker address
    memset(&worker_addr_, 0, sizeof(worker_addr_));
    worker_addr_.sin_family = AF_INET;
    worker_addr_.sin_addr.s_addr = backend_.GetWorkerEndpointAddr().ip;
    worker_addr_.sin_port = htons(port_);
    
    // Bind socket to worker address
    if (bind(socket_fd_, (struct sockaddr*)&worker_addr_, sizeof(worker_addr_)) < 0) {
        LOG(ERROR) << "Failed to bind socket: " << strerror(errno);
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    // Configure switch address
    memset(&switch_addr_, 0, sizeof(switch_addr_));
    switch_addr_.sin_family = AF_INET;
    switch_addr_.sin_addr.s_addr = backend_.GetSwitchEndpointAddr().ip;
    switch_addr_.sin_port = backend_.GetSwitchEndpointAddr().port;
    
    VLOG(0) << "UDP worker thread " << tid_ << " initialized socket on port " << port_;
    return true;
}

void UdpWorkerThread::ProcessJob(Job* job) {
    JobSlice* slice = nullptr;
    
    // Get the slice for this worker thread
    if (!job->GetSlice(&slice, tid_)) {
        LOG(ERROR) << "Failed to get job slice for worker thread " << tid_;
        return;
    }
    
    // Process the job slice
    ProcessJobSlice(job, slice);
}

void UdpWorkerThread::ProcessJobSlice(Job* job, JobSlice* job_slice) {
    DVLOG(1) << "UDP worker thread " << tid_ << " processing job slice";
    
    // Check if instant job completion is enabled
    if (config_.general_.instant_job_completion) {
        job_slice->MarkAsComplete();
        return;
    }
    
    // Send packets
    SendPackets(job, job_slice);
    
    // Mark job slice as complete
    job_slice->MarkAsComplete();
}

void UdpWorkerThread::CreateUdpPacketFromJobSlice(UdpBackend::UdpPacket& packet, 
                                                 Job* job, 
                                                 JobSlice* slice, 
                                                 uint64_t pkt_id) {
    packet.pkt_id = pkt_id;
    packet.job_id = job->GetId();
    packet.numel = config_.general_.packet_numel;
    packet.data_type = job->GetDataType();
    
    // Calculate offset in the job slice
    uint64_t offset = pkt_id * config_.general_.packet_numel;
    void* data_ptr = static_cast<char*>(slice->GetDataPtr()) + 
                   (offset * GetDataTypeSize(job->GetDataType()));
    
    packet.entries_ptr = data_ptr;
    packet.extra_info_ptr = nullptr;
    
    // Fill header
    packet.header.job_type_size = (static_cast<uint8_t>(job->GetType()) << 4) | 0x0F;
    packet.header.short_job_id = job->GetId() & 0xFF;
    packet.header.pkt_id = htonl(pkt_id);
    packet.header.switch_pool_index = 0;  // Will be set by switch
}

void UdpWorkerThread::SendPackets(Job* job, JobSlice* job_slice) {
    // Calculate how many packets needed for this job slice
    uint64_t total_packets = job_slice->GetNumel() / config_.general_.packet_numel;
    if (job_slice->GetNumel() % config_.general_.packet_numel != 0) {
        total_packets++;
    }
    
    // Calculate max outstanding packets per worker thread
    uint64_t max_outstanding = config_.general_.max_outstanding_packets / 
                             config_.general_.num_worker_threads;
    if (max_outstanding < 1) max_outstanding = 1;
    
    // Buffer for packet data
    const size_t pkt_hdr_size = sizeof(UdpBackend::UdpPacketHdr);
    const size_t elem_size = GetDataTypeSize(job->GetDataType());
    const size_t pkt_data_size = config_.general_.packet_numel * elem_size;
    const size_t pkt_size = pkt_hdr_size + pkt_data_size;
    std::unique_ptr<char[]> pkt_buffer(new char[pkt_size]);
    std::unique_ptr<char[]> recv_buffer(new char[pkt_size]);
    
    // Keep track of packets sent and received
    uint64_t packets_sent = 0;
    uint64_t packets_received = 0;
    
    std::vector<UdpBackend::UdpPacket> send_packets;
    std::vector<UdpBackend::UdpPacket> recv_packets;
    
    // Set up timeout for socket receives
    struct timeval tv;
    tv.tv_sec = 1;  // 1 second timeout
    tv.tv_usec = 0;
    setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    
    while (packets_received < total_packets) {
        // Fill the send buffer with more packets if needed and if we haven't sent all packets yet
        while (send_packets.size() < max_outstanding && packets_sent < total_packets) {
            UdpBackend::UdpPacket packet;
            CreateUdpPacketFromJobSlice(packet, job, job_slice, packets_sent);
            send_packets.push_back(packet);
            packets_sent++;
        }
        
        // Send packets directly to the switch
        if (!send_packets.empty()) {
            for (auto& packet : send_packets) {
                // Preprocess the packet data
                ppp_->ProcessForSending(packet.data_type, 
                                       packet.entries_ptr, 
                                       packet.numel, 
                                       packet.extra_info_ptr);
                
                // Copy header and data to buffer
                memcpy(pkt_buffer.get(), &packet.header, pkt_hdr_size);
                memcpy(pkt_buffer.get() + pkt_hdr_size, packet.entries_ptr, pkt_data_size);
                
                // Send the packet to the switch
                if (!SendPacket(pkt_buffer.get(), pkt_size)) {
                    LOG(ERROR) << "Failed to send packet to switch";
                    continue;
                }
                
                // Only try to receive if we're not in simulation mode
                if (!config_.backend_.udp.process_packets) {
                    // Wait for response from switch with aggregated values
                    size_t recv_size = pkt_size;
                    if (ReceivePacket(recv_buffer.get(), recv_size)) {
                        // Copy the received data back into the packet data buffer
                        memcpy(packet.entries_ptr, recv_buffer.get() + pkt_hdr_size, pkt_data_size);
                        
                        // Post-process the received data
                        ppp_->ProcessAfterReceiving(packet.data_type,
                                                  packet.entries_ptr,
                                                  packet.numel,
                                                  packet.extra_info_ptr);
                        packets_received++;
                    } else {
                        LOG(WARNING) << "Failed to receive response for packet ID " << packet.pkt_id;
                    }
                } else {
                    // In simulation mode, use the backend's simulation
                    recv_packets.clear();
                    backend_.SendBurst(tid_, {packet});  // Send a single packet to simulate
                    backend_.ReceiveBurst(tid_, recv_packets);  // Receive simulated response
                    
                    if (!recv_packets.empty()) {
                        // Process the simulated received packet
                        ppp_->ProcessAfterReceiving(recv_packets[0].data_type,
                                                  recv_packets[0].entries_ptr,
                                                  recv_packets[0].numel,
                                                  recv_packets[0].extra_info_ptr);
                        packets_received++;
                    }
                }
            }
            
            send_packets.clear();
        }
        
        // If we're not making progress, sleep a bit to avoid CPU spinning
        if (packets_sent >= total_packets && packets_received < total_packets) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } 
                                      packet.numel, 
                                      packet.extra_info_ptr);
            packets_received++;
        }
        
        // If we have sent all packets but not received all, sleep a bit to avoid busy waiting
        if (packets_sent >= total_packets && packets_received < total_packets) {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }
}

bool UdpWorkerThread::SendPacket(const void* buffer, size_t size) {
    ssize_t sent = sendto(socket_fd_, buffer, size, 0, 
                        (struct sockaddr*)&switch_addr_, 
                        sizeof(switch_addr_));
    return sent == static_cast<ssize_t>(size);
}

bool UdpWorkerThread::ReceivePacket(void* buffer, size_t& size) {
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    
    ssize_t received = recvfrom(socket_fd_, buffer, size, 0, 
                              (struct sockaddr*)&from_addr, 
                              &from_len);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return false;  // No data available
        }
        LOG(ERROR) << "Error receiving packet: " << strerror(errno);
        return false;
    }
    
    size = received;
    return true;
}

} // namespace switchml
