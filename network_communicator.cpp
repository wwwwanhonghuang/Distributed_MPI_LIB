#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <memory>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <thread>
#include <yaml-cpp/yaml.h>
#include <condition_variable>
#include <functional>
#include "common/message.h"
#include "shared_memory.hpp"
#include "communication/client.hpp"
#include "communication/sync.hpp"
#include "global_variables.hpp"
#include "services.hpp"
#include "message_impl.h"

using namespace GlobalState;

void push_msg_to_shared_memory_rr(Message& msg, std::shared_ptr<SharedMemory> shared_memory){
    static int pos = 0;
    auto storage = (MemoryStorage*) shared_memory->get_data();
   
    while(storage->application_messages[pos].status != EMPTY_SLOT){
        pos = (pos + 1) % MSG_COUNT;
    }

    std::cout << "find msg position " << pos << std::endl;
    memcpy(&storage->application_messages[pos], &msg, sizeof(msg));
}


void serverLoop(uint32_t port) {
    // Create a server socket
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Server socket creation failed");
        return;
    }

    // Configure the server address
    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Bind the socket to the specified port
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_sock);
        return;
    }

    // Listen for incoming connections
    if (listen(server_sock, 5) < 0) {
        perror("Listen failed");
        close(server_sock);
        return;
    }

    std::cout << "Server is listening on port " << port << "\n";

    while (keep_running) {
        // Set the socket to non-blocking
        fd_set read_fds;
        struct timeval timeout = {1, 0}; // 1-second timeout
        FD_ZERO(&read_fds);
        FD_SET(server_sock, &read_fds);

        int activity = select(server_sock + 1, &read_fds, NULL, NULL, &timeout);
        if (activity < 0 && errno != EINTR) {
            perror("Select error");
            break;
        }

        if (activity > 0 && FD_ISSET(server_sock, &read_fds)) {
            // Accept a connection
            struct sockaddr_in client_addr = {};
            socklen_t client_len = sizeof(client_addr);
            int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
            if (client_sock < 0) {
                perror("Accept failed");
                continue;
            }

            std::cout << "Connection accepted" << "client socket = " << client_sock << ", creating thread to handle client.\n";
            std::thread client_thread(handle_client, client_sock);
            client_thread.detach();
        }
    }
}

void broadcast_message(const Message& message) {
    client_map.lock();
    for (const auto& [sock, client] : client_map.value) {
        ssize_t bytes_sent = send(client.sock, &message, sizeof(message), 0);
        if (bytes_sent < 0) {
            perror(("Failed to send message to " + client.name).c_str());
        } 
    }
}

void connect_to_other_partitions(int& total_clients, int& connected_client, 
        int& client_index, const YAML::Node& clients, 
        int partition_id, const std::string& program_name){
    while(connected_client < total_clients) {
        const YAML::Node& client = clients[client_index];
        std::string name = client["name"].as<std::string>();
        client_index = (client_index + 1) % total_clients;
        if (name == program_name) continue;
        client_map.lock();
        if(client_map.value.find(partition_id) != client_map.value.end()) continue;
        std::string ip = client["ip"].as<std::string>();
        uint32_t port = client["port"].as<uint32_t>();
        // std::cout << ip << " " << port << " " << name << std::endl;
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("Socket creation failed");
            continue;
        }
        struct sockaddr_in client_addr = {};
        client_addr.sin_family = AF_INET;
        client_addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &client_addr.sin_addr);

        if (connect(sock, (struct sockaddr*)&client_addr, sizeof(client_addr)) == 0) {
            std::cout << "\t- connect " << ip << ":" << port << " success." << " sock ="
                << sock << " \n";
            connected_client ++;
            Client client;
            client.state = CONNETED;
            client.sock = sock;
            {
                // Lock the mutex to safely modify the shared client_map
                client_map.value[sock] = client;
            }
        } else {
            // perror("\t- connect failed");
            close(sock);
        }
    }
    
    while(true){
        client_map.lock();
        if(client_map.value.size() < total_clients - 1) continue;
        break;
    }

    std::cout << "All clients connected. " << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Please provide the instance index (i).\n";
        return 1;
    }

    int partition_id = std::stoi(argv[1]);
    YAML::Node config = YAML::LoadFile("cluster.yaml");
    std::string program_name = std::string("pcfg-train-") + std::to_string(partition_id);
    uint32_t server_port = 9239 + partition_id;
    std::cout << "Creating server for self at port " << server_port << "\n";
    std::thread server_thread(serverLoop, server_port);
    const YAML::Node& clients = config["cluster"]["clients"];
    int total_clients = clients.size();
    int connected_client = 1;
    int client_index = 0;

    connect_to_other_partitions(total_clients, connected_client, client_index, clients, partition_id, program_name);


    std::cout << "Open share memory. " << std::endl;
    int size = sizeof(MemoryStorage);
    auto shared_memory = std::make_shared<SharedMemory>(program_name.c_str(), CREATE_NEW, size);
    auto storage = (MemoryStorage*)shared_memory->get_data();
   
    /* 1. Notify the application network topology are prepared and connected. */
    Message network_component_prepared_msg = gen_network_component_prepared_msg(partition_id);
    push_msg_to_shared_memory_rr(network_component_prepared_msg, shared_memory);

    /* 2. Wait ACK from the application */
    while(storage->network_communicator_messages[0].status == EMPTY_SLOT){}
    std::cout << "application repied: " <<  
        storage->network_communicator_messages[0].data << std::endl;
    storage->network_communicator_messages[0].status = EMPTY_SLOT;

    /* 3. Broadcast partition prepared message to all other partitions. */
    std::cout << "Broadcast prepared message." << std::endl;
    Message partition_prepared_msg = gen_partition_prepared_msg(partition_id);
    broadcast_message(partition_prepared_msg);

    /* 4. Wait ACKs of PARTITION_PREPARED . */
    std::cout << total_clients << std::endl;
    {
        std::unique_lock<std::mutex> lock = partition_prepared_msg_ack_count.lock();
        partition_prepared_msg_cv.wait(lock, [&total_clients] { return partition_prepared_msg_ack_count.value == total_clients - 1; });
    }
    std::cout << "[barrier passed] All partition prepared!" << std::endl;

    int epoch = 0;
    const int MAX_EPOCHS = 3;
    while(epoch < MAX_EPOCHS){
        std::cout << std::endl;
        std::cout << "[Main Loop] partition " << program_name << " begin epoch " << epoch << std::endl;
        
        
        // 5.1 Notify Application begin a new epoch.
        Message epoch_begin_msg = gen_epoch_begin_message(epoch, partition_id);
        push_msg_to_shared_memory_rr(epoch_begin_msg, shared_memory);
        broadcast_message(epoch_begin_msg);
        {
            std::unique_lock<std::mutex> lock = begin_epoch_msg_ack_count.lock();
            begin_epoch_msg_cv.wait(lock, [&total_clients, &epoch] { return begin_epoch_msg_ack_count.value[epoch] == total_clients - 1; });
        }
        std::cout << "[Main Loop] [barrier passed] All partition prepare to proceed epoch " << epoch << "!" << std::endl;
        
        // 5.2 Wait application finished.
        std::cout << "[Main Loop] wait application execution. " << std::endl;
        {
            std::unique_lock<std::mutex> lock(application_mutex);
            while(storage->network_communicator_messages[0].status == EMPTY_SLOT){
            }
            storage->network_communicator_messages[0].status = EMPTY_SLOT;
        }

        int application_result = -1;
        memcpy(&application_result, storage->network_communicator_messages[0].data, sizeof(int));
        
        std::cout << "[Main Loop] application reply result " <<  
            application_result << std::endl;
        
        storage->network_communicator_messages[0].status = EMPTY_SLOT;

        // 5.3 Broadcast epoch finished message.
        Message epoch_finished_msg = gen_epoch_finished_msg(partition_id, epoch, application_result);
        std::cout << "[Main Loop] Prepare and broadcast epoch " << epoch << "finished message." << std::endl;
        broadcast_message(epoch_finished_msg);

        // 5.4 Wait clients until all clients finish current epoch.
        {
            auto lock = epoch_completed_ack_count.lock();
            epoch_completed_msg_cv.wait(lock, [&total_clients, &epoch] { return epoch_completed_ack_count.value[epoch] == total_clients - 1; });
        }
        std::cout << "[Main Loop] [barrier passed] All partition Completed Epoch " << epoch << "!" << std::endl;
        std::cout << "[Main Loop] Integrated Result = " << global_result.get()  << " + " <<
                application_result << "!" << std::endl;

        // 5.5 Notify the application the integrated results.
        int integrated_result = global_result.get() + application_result;
        Message msg_integrated_result_notification = gen_notificate_integrate_result_msg(integrated_result);
        push_msg_to_shared_memory_rr(msg_integrated_result_notification, shared_memory);

        // 5.6 Wait for ack from application
        {
            std::unique_lock<std::mutex> lock(application_mutex);
            while(storage->network_communicator_messages[0].status == EMPTY_SLOT){
            }
            storage->network_communicator_messages[0].status = EMPTY_SLOT;
        }
        std::cout << "[Main Loop] Integrated result processed by application." << std::endl;

        epoch ++;
        {
            global_result.lock();
            global_result.value = 0;
        }

        std::cout << std::endl;
    }

    std::cin.get();
    abort();

    return 0;
}