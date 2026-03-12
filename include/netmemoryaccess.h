#pragma once
#include <common.h>
#include <wii/os/OSThread.h>

namespace NetMemoryAccess {

typedef enum {
    READ,
    WRITE
} MemoryAccessMode;

#define PORT 5555
#define READ_CHUNK_SIZE 8
#define BUFSIZE 1024
#define STACK_SIZE 0x8000
#define MAX_INIT_RETRIES 32
#define INIT_RETRY_SLEEP 729000000
#define NETMEMORYACCESS_EOF '\n'
#define NETMEMORYACCESS_EOF_SIZE sizeof(char) //msl::string::strlen(NETMEMORYACCESS_EOF)

#define MAX_OUTGOING 32

struct OutgoingPacket {
    u16 cmdID;
    u16 length;
    u8 data[256];
};

extern OutgoingPacket outgoingPackets[MAX_OUTGOING];
extern int goutgoingHead;
extern int goutgoingTail;

bool enqueuePacket(
    u16 cmdId,
    const void* payload,
    u16 payloadLen
);

extern u8 stack[STACK_SIZE];
extern wii::os::OSThread thread;

extern "C" {
    void init();
    s32 initializeNetwork();
    void receiverLoop(u32 param);
}

}