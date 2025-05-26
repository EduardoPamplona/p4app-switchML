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
 * @file udp_backend.h
 * @brief Declares the UdpBackend class.
 */

#ifndef SWITCHML_UDP_BACKEND_H_
#define SWITCHML_UDP_BACKEND_H_

#ifndef UDP
#define UDP
#endif

#include <memory>
#include <vector>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"
#include "context.h"
#include "backend.h"
#include "grpc_client.h"

namespace switchml {

// Forward declare thread classes
class UdpWorkerThread;

/**
 * @brief The backend that implements UDP sockets for SwitchML communication
 */
class UdpBackend : public switchml::Backend {
  public:
    /**
     * @brief The SwitchML UDP packet header.
     */
    struct UdpPacketHdr {
        /**
         * Combined field for job type and packet size
         * 4 MSBs for job type, 4 LSBs for size
         */
        uint8_t job_type_size;
        
        /** 8 LSBs of job ID - for duplicate detection */
        uint8_t short_job_id;
        
        /** Packet ID within job slice */
        uint32_t pkt_id;
        
        /** Switch pool/slot index */
        uint16_t switch_pool_index;
    } __attribute__((__packed__));

    /** Type for packet elements */
    typedef int32_t UdpPacketElement;

    /**
     * @brief Endpoint address structure 
     */
    struct EndpointAddress {
        /** IP address in network byte order */
        uint32_t ip;
        /** Port in network byte order */
        uint16_t port;
    };

    /**
     * @brief UDP packet structure for transmission
     */
    struct UdpPacket {
        /** Packet header */
        UdpPacketHdr header;
        
        /** Packet ID within job slice */
        uint64_t pkt_id;
        
        /** Job ID */
        JobId job_id;
        
        /** Number of elements */
        Numel numel;
        
        /** Data type */
        DataType data_type;
        
        /** Pointer to data elements */
        void* entries_ptr;
        
        /** Pointer to extra info */
        void* extra_info_ptr;
    };

    /**
     * @brief Constructor
     */
    UdpBackend(Context& context, Config& config);
    
    ~UdpBackend();
    
    UdpBackend(UdpBackend const&) = delete;
    void operator=(UdpBackend const&) = delete;
    
    UdpBackend(UdpBackend&&) = default;
    UdpBackend& operator=(UdpBackend&&) = default;

    /**
     * @brief Setup worker threads
     */
    void SetupWorker() override;
    
    /**
     * @brief Wait for all worker threads to finish
     */
    void CleanupWorker() override;
    
    /**
     * @brief Configure the switch via the controller
     */
    void SetupSwitch();
    
    /**
     * @brief Get reference to switch endpoint address
     */
    struct EndpointAddress& GetSwitchEndpointAddr();
    
    /**
     * @brief Get reference to worker endpoint address
     */
    struct EndpointAddress& GetWorkerEndpointAddr();
    
    /**
     * @brief Send a burst of packets
     */
    void SendBurst(WorkerTid worker_thread_id, const std::vector<UdpPacket>& packets);
    
    /**
     * @brief Receive a burst of packets
     */
    void ReceiveBurst(WorkerTid worker_thread_id, std::vector<UdpPacket>& packets_received);
    
    /**
     * @brief Get worker threads
     */
    std::vector<UdpWorkerThread>& GetWorkerThreads();

  private:
    /** Worker threads */
    std::vector<UdpWorkerThread> worker_threads_;
    
    /** Switch address */
    struct EndpointAddress switch_endpoint_addr_;
    
    /** Worker address */
    struct EndpointAddress worker_endpoint_addr_;
    
    /** GRPC client for controller communication */
    GrpcClient grpc_client_;
    
    /** Pending packets (sent but not received) */
    std::vector<UdpPacket>* pending_packets_;
};

} // namespace switchml
#endif // SWITCHML_UDP_BACKEND_H_
