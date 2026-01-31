#include <common.h>
#include <spm/memory.h>
#include <spm/system.h>
#include <msl/stdio.h>
#include <msl/string.h>
#include <wii/os/OSError.h>
#include <wii/os/OSThread.h>
#include <wii/vi.h>
#include <stddef.h>
#include <stdlib.h>

#include "commandmanager.h"
#include "core_http_client.h"
#include "core_json.h"
#include "errno.h"
#include "network.h"
#include "netmemoryaccess.h"
#include "spmhttp.h"

namespace NetMemoryAccess {

u8 stack[STACK_SIZE];
wii::os::OSThread thread;

void init() {
    wii::os::OSCreateThread(&thread, (wii::os::ThreadFunc*)receiverLoop, 0, stack + STACK_SIZE, STACK_SIZE, 24, 1);
    wii::os::OSResumeThread(&thread);
}

s32 initializeNetwork() {    
    s32 i, res;
    for (i = 0; i < MAX_INIT_RETRIES; i++) {
        res = Mynet_init();
        if ((res == -EAGAIN) || (res == -ETIMEDOUT)) {
            usleep(INIT_RETRY_SLEEP);
            continue;
        }
        else break;
    }

    return res;
}

void receiverLoop(u32 param) {
    (void) param;
    
    wii::os::OSReport("Initializing network...\n");

    s32 res = initializeNetwork();

    if (res < 0) {
        wii::os::OSReport("Initializing network failed. %d\n", res);
        return;
    }
    else {
        wii::os::OSReport("Network initialized successfully.\n");
    }

    mod::initCommands();
    
    int listenfd = 0, connfd = 0;
    struct sockaddr_in serv_addr;
    u32 addrlen = sizeof(serv_addr);

    u8 sendBuff[BUFSIZE];
    u8 recvBuff[BUFSIZE];

    listenfd = Mynet_socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        return;
    }

    msl::string::memset(&serv_addr, 0, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT); 

    if (Mynet_bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) {
        Mynet_close(listenfd);
        return;
    }

    if (Mynet_listen(listenfd, 10)) {
        Mynet_close(listenfd);
        return;
    }

    s32 size;
    while (1)
    {
        msl::string::memset(sendBuff, 0, BUFSIZE);
        msl::string::memset(recvBuff, 0, BUFSIZE);

        do {
            wii::os::OSYieldThread();
            connfd = Mynet_accept(listenfd, (struct sockaddr*)&serv_addr, &addrlen);
        } while (connfd < 0);

        int totalReceived = 0;
        bool ok = true;

        // read header (4 bytes)
        while (totalReceived < (int)(sizeof(u16) * 2)) {
            int bytes = Mynet_read(
                connfd,
                recvBuff + totalReceived,
                (sizeof(u16) * 2) - totalReceived
            );

            if (bytes <= 0) { ok = false; break; }
            totalReceived += bytes;
        }

        u16 packetLength = 0;

        if (ok) {
            // extract packet_length
            msl::string::memcpy(&packetLength, recvBuff + sizeof(u16), sizeof(u16));

            if (packetLength > BUFSIZE || packetLength < sizeof(u16) * 2) {
                wii::os::OSReport("Bad packet length: %u\n", packetLength);
                ok = false;
            }
        }

        if (ok) {
            // read remaining bytes
            while (totalReceived < (int)packetLength) {
                int bytes = Mynet_read(
                    connfd,
                    recvBuff + totalReceived,
                    packetLength - totalReceived
                );

                if (bytes <= 0) { ok = false; break; }
                totalReceived += bytes;
            }
        }

        if (ok) {
            auto* commandManager = mod::CommandManager::Instance();
            size = commandManager->parseAndExecute(
                recvBuff,
                packetLength,
                sendBuff,
                BUFSIZE
            );

            Mynet_write(connfd, sendBuff, size);
        }

        Mynet_close(connfd);
        wii::os::OSYieldThread();
    }

    }

}
