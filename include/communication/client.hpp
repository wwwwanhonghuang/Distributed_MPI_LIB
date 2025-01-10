#ifndef CLIENT_HPP
#define CLIENT_HPP
typedef enum {
    UNCONNECTED,
    CONNETED
} ClientStates;

struct Client
{
    ClientStates state;
    int sock;
    std::string name;
};
#endif