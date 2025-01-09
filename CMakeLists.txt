# Set the minimum CMake version
cmake_minimum_required(VERSION 3.10)

# Set the project name and version
project(SharedMemoryExample VERSION 1.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Specify the include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Add source files for shared memory library
set(SHARED_MEMORY_SRC src/shared_memory.cpp)
set(NETWORK_COMMUNICATOR_SRC network_communicator.cpp)
# Create a static library from shared memory source files
add_library(shared_memory STATIC ${SHARED_MEMORY_SRC})

set(APPLICATION_SRC application.cpp)

add_executable(application ${APPLICATION_SRC})
target_link_libraries(application shared_memory)

add_executable(network_communicator ${NETWORK_COMMUNICATOR_SRC})
target_link_libraries(network_communicator shared_memory yaml-cpp)
add_compile_options(-O0 -g)