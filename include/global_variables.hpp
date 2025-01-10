#ifndef GLOBAL_VARIABLES_HPP
#define GLOBAL_VARIABLES_HPP

#include <atomic>
#include <unordered_map>
#include <condition_variable>
#include <mutex>
#include "communication/sync.hpp"
#include "communication/client.hpp"

namespace GlobalState {
    inline std::atomic<bool> keep_running(true);

    inline MutexableVariable<std::unordered_map<int, Client>> client_map;
    inline MutexableVariable<int> global_result;

    inline std::condition_variable partition_prepared_msg_cv;
    inline MutexableVariable<int> partition_prepared_msg_ack_count;

    inline std::condition_variable begin_epoch_msg_cv;
    inline MutexableVariable<std::unordered_map<int, int>> begin_epoch_msg_ack_count;

    inline std::condition_variable epoch_completed_msg_cv;
    inline MutexableVariable<std::unordered_map<int, int>> epoch_completed_ack_count;

    inline std::condition_variable application_cv;
    inline std::mutex application_mutex;
}

#endif
