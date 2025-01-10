#include "services.hpp"
#include "message.h"
#include "global_variables.hpp"
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <memory>
#include <thread>
#include <cstring>
using namespace GlobalState;

void handle_client(int client_sock) {
    std::cout << "Handling client in thread: " << std::this_thread::get_id() << "\n";

    Message msg_receive;
    while(true){
        ssize_t bytes_read = read(client_sock, &msg_receive, sizeof(Message));
        if (bytes_read > 0) {
            std::cout << "Received: " << "msg_type = " << msg_receive.msg_type << "\n";
            switch (msg_receive.msg_type)
            {
            case PARTITION_PREPARED: {
                {
                    partition_prepared_msg_ack_count.lock();
                    partition_prepared_msg_ack_count.value ++;
                    std::cout << "ACK " << PARTITION_PREPARED << " partition_prepared_msg_ack_count = " << 
                                partition_prepared_msg_ack_count.value << std::endl;
                }
                partition_prepared_msg_cv.notify_one(); 
                break;
            }
            case EPOCH_COMPLETE: {
                int epoch = -1;
                int client_partition_id = -1;
                int client_result = -1;
                memcpy(&client_partition_id, msg_receive.data, sizeof(int));
                memcpy(&epoch, &msg_receive.data[4], sizeof(int));
                memcpy(&client_result, &msg_receive.data[8], sizeof(int));

                std::cout << "[Client Service] client " <<  client_partition_id << " has completed epoch " << epoch <<
                            " reported result = " << client_result << std::endl;
                {
                    global_result.lock();
                    global_result.value += client_result;
                }
                {
                    epoch_completed_ack_count.lock();
                    epoch_completed_ack_count.value[epoch]++;
                    std::cout << "ACK " << EPOCH_COMPLETE << " epoch_completed_ack_count[" << epoch << 
                            "] = " << epoch_completed_ack_count.value[epoch] << std::endl;
                }
                std::cout << "[Client Service] Notify one thread that waiting for barrier_cv" << std::endl;
                epoch_completed_msg_cv.notify_one();
                break;
            }
            case BEGIN_EPOCH:{
                int epoch = -1;
                int client_partition_id = -1;
                memcpy(&client_partition_id, &msg_receive.data[4], sizeof(int));
                memcpy(&epoch, &msg_receive.data[0], sizeof(int));
                std::cout << "[Client Service] client " <<  client_partition_id << " prepare proceed epoch " << epoch << std::endl;
                {
                    begin_epoch_msg_ack_count.lock();
                    begin_epoch_msg_ack_count.value[epoch]++;
                    std::cout << "ACK " << BEGIN_EPOCH << " begin_epoch_msg_ack_count[" << epoch <<
                        "] = " << begin_epoch_msg_ack_count.value[epoch] << std::endl;
                }
                
                std::cout << "[Client Service] Notify one thread that waiting for barrier_cv" << std::endl;
                begin_epoch_msg_cv.notify_one();
                break;
            }
            default:
                break;
            }
        }
    }
}
