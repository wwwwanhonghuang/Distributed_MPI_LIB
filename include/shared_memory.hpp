#ifndef SHAREDMEMORY_HPP
#define SHAREDMEMORY_HPP

#include "common/memory_region.h"
#include "common/message.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>


#define MSG_COUNT 16

typedef enum {
    CREATE_NEW,
    MAP_TO_EXIST
} MEM_MODEL;

typedef struct {
    Message application_messages[MSG_COUNT];
    Message network_communicator_messages[MSG_COUNT];
    uint32_t status;
} MemoryStorage;

class SharedMemory : public MemoryRegion {
public:
    SharedMemory(const char* shm_name, MEM_MODEL model, size_t size);
    ~SharedMemory();

    void* getBeginAddress() override;
    size_t getLength() override;
    void read(void* destination, size_t offset, size_t length) override;
    void write(void* source, size_t offset, size_t length) override;

    void* get_data();
private:
    const char* shm_name;
    size_t size;
    int shm_fd;
    MemoryStorage* data;
};

#endif
