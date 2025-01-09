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
#include "message.h"
#include "shared_memory.hpp"
#include <thread>
#include <yaml-cpp/yaml.h>
#include <condition_variable>


typedef enum {
    UNCONNECTED,
    CONNETED
} ClientStates;

typedef enum {
    PARTITION_PREPARED = 102,

} MessageType;

struct Client
{
    ClientStates state;
    int sock;
    std::string name;
};

std::atomic<bool> keep_running(true);
std::unordered_map<int, Client> client_map;

std::mutex client_map_mutex;
std::condition_variable barrier_cv;
std::mutex barrier_mutex;
int ack_count = 0;

void push_msg(Message& msg, std::shared_ptr<SharedMemory> shared_memory){
    static int pos = 0;
    auto storage = (MemoryStorage*) shared_memory->get_data();
   
    while(storage->application_messages[pos].status != EMPTY_SLOT){
        pos = (pos + 1) % MSG_COUNT;
    }

    std::cout << "find msg position " << pos << std::endl;
    memcpy(&storage->application_messages[pos], &msg, sizeof(msg));
}
void handleClient(int client_sock) {
    // Placeholder for client handling logic
    std::cout << "Handling client in thread: " << std::this_thread::get_id() << "\n";

    // Example: Echo received data back to the client
    Message msg_receive;
    ssize_t bytes_read = read(client_sock, &msg_receive, sizeof(Message));
    if (bytes_read > 0) {
        std::cout << "Received: " << "msg_type = " << msg_receive.msg_type << "\n";
        switch (msg_receive.msg_type)
        {
        case PARTITION_PREPARED: {
            {
                std::lock_guard<std::mutex> lock(barrier_mutex);
                ack_count++;
            }
            barrier_cv.notify_one(); // Notify the main thread
            break;
        }
        default:
            break;
        }
    }

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
            
            std::thread client_thread(handleClient, client_sock);
            client_thread.detach();
        }
    }
}

void broadcast_message(const Message& message) {
    std::lock_guard<std::mutex> lock(client_map_mutex);
    for (const auto& [sock, client] : client_map) {
        std::cout << "Sending message to " << client.name << " (socket " << client.sock << ")\n";

        // Send the message
        ssize_t bytes_sent = send(client.sock, &message, sizeof(message), 0);
        if (bytes_sent < 0) {
            perror(("Failed to send message to " + client.name).c_str());
        } else {
            std::cout << "Message sent to " << client.name << ": " << bytes_sent << " bytes\n";
        }
    }
}


Message gen_network_component_prepared_msg(int partition_id){
    Message msg;
    msg.status = MESSAGE_WAITING; 
    msg.msg_type = 100;
    memcpy(msg.data, &partition_id, sizeof(int));
    return msg;
};

Message gen_partition_prepared_msg(int partition_id){
    Message prepared_msg;
    prepared_msg.status = MESSAGE_WAITING;
    prepared_msg.msg_type = 102;
    memcpy(prepared_msg.data, &partition_id, sizeof(int));
    return prepared_msg;
};


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

    while(connected_client < total_clients) {
        const YAML::Node& client = clients[client_index];
        std::string name = client["name"].as<std::string>();
        client_index = (client_index + 1) % total_clients;
        if (name == program_name) continue;
        if(client_map.find(partition_id) != client_map.end()) continue;

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
                std::lock_guard<std::mutex> lock(client_map_mutex);
                client_map[sock] = client;
            }
        } else {
            // perror("\t- connect failed");
            close(sock);
        }
    }
    
    while(client_map.size() < total_clients - 1){}
    std::cout << "All clients connected. " << std::endl;
    std::cout << "Open share memory. " << std::endl;

    int size = sizeof(MemoryStorage);
    auto shared_memory = std::make_shared<SharedMemory>(program_name.c_str(), CREATE_NEW, size);
    auto storage = (MemoryStorage*)shared_memory->get_data();
   
    /* communicate with application. */
    Message network_component_prepared_msg = gen_network_component_prepared_msg(partition_id);
    push_msg(network_component_prepared_msg, shared_memory);

    /* wait for application response */
    while(storage->network_communicator_messages[0].status == EMPTY_SLOT){}
    std::cout << "application repied: " <<  
                storage->network_communicator_messages[0].data << std::endl;


    /* broadcast partition prepared message to all other partitions. */
    std::cout << "Broadcast prepared message." << std::endl;
    Message partition_prepared_msg = gen_partition_prepared_msg(partition_id);
    broadcast_message(partition_prepared_msg);
    // barrier
    // wait for all client replay ack for PARTITION_PREPARED message.
    {
        std::unique_lock<std::mutex> lock(barrier_mutex);
        barrier_cv.wait(lock, [&total_clients] { return ack_count == total_clients - 1; });
    }
    std::cout << "[barrier passed] All partition prepared!" << std::endl;


    // broadcast partition prepared_msg
    /*
        for each client in clients:
            send_msg("prepared from xxxx");
    */


    // shared std::unordered_set<std::string> host_names;
    // thread on_client_msg_in() = {
    //     msg <- ..
    //     host_names.emplace(get_program_name(msg));
    // }

    // barrier: ensure all partition prepared.
    // while(host_names.size() < total_clients){
    //     std::cout << storage->network_communicator_messages[0].data << std::endl;
    // }

    std::cin.get();
    abort();
    
    

    return 0;
}