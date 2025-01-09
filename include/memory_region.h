#ifndef MEMORYREGION_H
#define MEMORYREGION_H

#include <cstring>
#include <iostream>

class MemoryRegion {
public:
    virtual void* getBeginAddress() = 0;
    virtual size_t getLength() = 0;
    virtual void read(void* destination, size_t offset, size_t length) = 0;
    virtual void write(void* source, size_t offset, size_t length) = 0;
    virtual ~MemoryRegion() {}
};

#endif 

