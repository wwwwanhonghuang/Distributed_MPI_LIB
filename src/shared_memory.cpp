#include "shared_memory.hpp"

SharedMemory::SharedMemory(const char* shm_name, MEM_MODEL model, size_t size)
    : shm_name(shm_name), size(size), shm_fd(-1), data(nullptr) {
    shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Failed to create/open shared memory");
        exit(1);
    }

    if(model == CREATE_NEW){
        if (ftruncate(shm_fd, size) == -1) {
            perror("Failed to set shared memory size");
            exit(1);
        }
    }

    data = (MemoryStorage*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    
    if (data == MAP_FAILED) {
        perror("Failed to map shared memory");
        exit(1);
    }
    memset(data, 0, size);
}

SharedMemory::~SharedMemory() {
    if (data != nullptr && munmap(data, size) == -1) {
        perror("Failed to unmap shared memory");
    }
    if (shm_fd != -1 && close(shm_fd) == -1) {
        perror("Failed to close shared memory file descriptor");
    }
    if (shm_unlink(shm_name) == -1) {
        perror("Failed to unlink shared memory object");
    }
}

void* SharedMemory::getBeginAddress() {
    return data;
}

size_t SharedMemory::getLength() {
    return size;
}

void SharedMemory::read(void* destination, size_t offset, size_t length) {
    if (offset + length > size) {
        std::cerr << "Read exceeds shared memory bounds.\n";
        exit(1);
    }
    std::memcpy(destination, (char*)data + offset, length);
}

void SharedMemory::write(void* source, size_t offset, size_t length) {
    if (offset + length > size) {
        std::cerr << "Write exceeds shared memory bounds.\n";
        exit(1);
    }
    std::memcpy((char*)data + offset, source, length);
}

void* SharedMemory::get_data() {
    return (void*)this->data;
}