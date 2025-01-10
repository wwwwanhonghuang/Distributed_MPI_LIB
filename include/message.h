#ifndef MESSAGE_H
#define MESSAGE_H
#define MSG_DATA_SIZE 128
#include <cstdint>
typedef enum {
    EMPTY_SLOT = 0,
    MESSAGE_WAITING = 1,   // Message is waiting to be processed
    MESSAGE_PROCESSING = 2, // Message is being processed
    MESSAGE_PROCESSED = 3   // Message is processed and can be cleared
} MessageStatus;

typedef enum {
    PARTITION_PREPARED = 100,
    BEGIN_EPOCH = 103,
    EPOCH_COMPLETE = 104,
    NOTIFICATE_INTEGRATE_RESULT = 105
} MessageType;

typedef struct {
    char data[MSG_DATA_SIZE];
    uint32_t msg_type;
    MessageStatus status; // Use enum for clarity
} Message;

#endif
