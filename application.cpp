#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <memory>
#include "shared_memory.hpp"
#include <string>
#define SHARED_MEMORY_NAME "/shared_mem"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Please provide the instance index (i).\n";
        return 1;
    }

    std::string program_name = std::string("pcfg-train-") +
            std::string(argv[1]);

    auto shared_memory = 
        std::make_shared<SharedMemory>(program_name.c_str(), CREATE_NEW, sizeof(MemoryStorage));
    
    std::cout << "shared memory created. Name = " << program_name << std::endl;
    auto storage = (MemoryStorage*)(shared_memory->get_data());
    
    int pos = 0;
    while(true){
        auto& msg = storage->application_messages[pos];
        pos = (pos + 1) % 1;
        if(msg.status == EMPTY_SLOT) continue;
        std::cout << "get msg, type = " << msg.msg_type << std::endl;
        switch (msg.msg_type)
        {
        case 100:{
            std::cout << "response msg" << std::endl;
            msg.status = EMPTY_SLOT;
            const std::string& response = "Hi! This is a response for msg 100";
            
            Message response_msg;
            response_msg.status = MESSAGE_WAITING; 
            response_msg.msg_type = 100;
            memcpy(response_msg.data, response.c_str(), response.size() + 1);
            memcpy(&storage->network_communicator_messages[0], &response_msg, sizeof(response_msg));
            break;
        }
        default:
            break;
        }
    }

    while(std::cin.get()){
        auto& msg = storage->application_messages[0];
        std::cout << "msg:" << msg.data << std::endl;
        std::cout << "type:" << msg.msg_type << std::endl;
        msg.status = EMPTY_SLOT;
    }
        
    return 0;
}
