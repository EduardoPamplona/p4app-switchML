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
 * @file udp_worker_thread.h
 * @brief Declares the UdpWorkerThread class.
 */

#ifndef SWITCHML_UDP_WORKER_THREAD_H_
#define SWITCHML_UDP_WORKER_THREAD_H_

#include <thread>
#include <memory>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>

#include "common.h"
#include "context.h"
#include "config.h"
#include "udp_backend.h"
#include "prepostprocessor.h"

namespace switchml {

/**
 * @brief Worker thread for UDP backend that handles communication with switch
 */
class UdpWorkerThread {
  public:
    /**
     * @brief Constructor
     */
    UdpWorkerThread(Context& context, UdpBackend& backend, Config& config, int thread_id);
    
    /**
     * @brief Destructor
     */
    ~UdpWorkerThread();
    
    UdpWorkerThread(UdpWorkerThread const&) = delete;
    void operator=(UdpWorkerThread const&) = delete;
    
    UdpWorkerThread(UdpWorkerThread&&) = default;
    UdpWorkerThread& operator=(UdpWorkerThread&&) = default;
    
    /**
     * @brief Start the worker thread
     */
    void Start();
    
    /**
     * @brief Join the worker thread
     */
    void Join();
    
    /**
     * @brief Worker thread main function
     */
    void operator()();
    
  private:
    /** Worker thread ID */
    int tid_;
    
    /** Worker context */
    Context& context_;
    
    /** Backend reference */
    UdpBackend& backend_;
    
    /** Configuration */
    Config& config_;
    
    /** Worker thread socket */
    int socket_fd_;
    
    /** Worker thread */
    std::unique_ptr<std::thread> thread_;
    
    /** Thread running flag */
    std::atomic<bool> running_;
    
    /** Thread port (base port + thread_id) */
    uint16_t port_;
    
    /** Pre/post processor */
    std::shared_ptr<PrePostProcessor> ppp_;
    
    /** Switch socket address */
    struct sockaddr_in switch_addr_;
    
    /** Worker socket address */
    struct sockaddr_in worker_addr_;
    
    /**
     * @brief Initialize socket
     */
    bool InitializeSocket();
    
    /**
     * @brief Process a job
     */
    void ProcessJob(Job* job);
    
    /**
     * @brief Process a job slice
     */
    void ProcessJobSlice(Job* job, JobSlice* job_slice);
    
    /**
     * @brief Create UDP packet from job slice
     */
    void CreateUdpPacketFromJobSlice(UdpBackend::UdpPacket& packet, Job* job, JobSlice* slice, uint64_t pkt_id);
    
    /**
     * @brief Send packets
     */
    void SendPackets(Job* job, JobSlice* job_slice);
    
    /**
     * @brief Send packet
     */
    bool SendPacket(const void* buffer, size_t size);
    
    /**
     * @brief Receive packet
     */
    bool ReceivePacket(void* buffer, size_t& size);
};

} // namespace switchml
#endif // SWITCHML_UDP_WORKER_THREAD_H_
