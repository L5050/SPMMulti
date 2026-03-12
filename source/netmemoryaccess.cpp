#include <common.h>
#include <spm/memory.h>
#include <spm/system.h>
#include <msl/stdio.h>
#include <msl/string.h>
#include <wii/os/OSMutex.h>
#include <wii/os/OSError.h>
#include <wii/os/OSThread.h>
#include <wii/vi.h>
#include <stddef.h>
#include <stdlib.h>

#include "commandmanager.h"
#include "core_http_client.h"
#include "core_json.h"
#include "spm_errno.h"
#include "errno.h"
#include "network.h"
#include "netmemoryaccess.h"
#include "spmhttp.h"

namespace NetMemoryAccess {

    OutgoingPacket outgoingPackets[MAX_OUTGOING]; 
    int goutgoingHead = 0; 
    int goutgoingTail = 0; 
    alignas(32) u8 stack[STACK_SIZE]; 
    wii::os::OSThread thread;

    // --- connection state shared by recv + send threads ---
    static wii::os::OSMutex gConnMutex;
    static bool gConnMutexInit = false;

    static volatile s32 gConnFd = -1;
    static volatile bool gConnAlive = false;

    // Dedicated sender thread
    static wii::os::OSThread gSendThread;
    alignas(32) static u8 gSendStack[STACK_SIZE];

    static wii::os::OSMutex gQueueMutex;
    static bool gQueueMutexInit = false;

    static inline void InitQueueMutexOnce()
    {
        if (!gQueueMutexInit) {
            wii::os::OSInitMutex(&gQueueMutex);
            gQueueMutexInit = true;
        }
    }

    static inline void InitConnMutexOnce()
    {
        if (!gConnMutexInit) {
            wii::os::OSInitMutex(&gConnMutex);
            gConnMutexInit = true;
        }
    }

    static inline void SetConnection(s32 fd)
    {
        OSLockMutex(&gConnMutex);
        gConnFd = fd;
        gConnAlive = (fd >= 0);
        OSUnlockMutex(&gConnMutex);
    }

    static inline void ClearConnection()
    {
        OSLockMutex(&gConnMutex);
        gConnAlive = false;
        gConnFd = -1;
        OSUnlockMutex(&gConnMutex);
    }

    static inline bool GetConnection(s32* outFd)
    {
        OSLockMutex(&gConnMutex);
        bool alive = gConnAlive;
        s32 fd = gConnFd;
        OSUnlockMutex(&gConnMutex);
        *outFd = fd;
        return alive && (fd >= 0);
    }

    static inline bool IsRetryableSockRet(int r)
    {
        return (r == -EAGAIN || r == -ETIMEDOUT
    #ifdef EWOULDBLOCK
                || r == -EWOULDBLOCK
    #endif
        );
    }

    static bool DequeueOutgoing(OutgoingPacket* out)
    {
        OSLockMutex(&gQueueMutex);

        if (goutgoingTail == goutgoingHead) {
            OSUnlockMutex(&gQueueMutex);
            return false;
        }

        *out = outgoingPackets[goutgoingTail];
        goutgoingTail = (goutgoingTail + 1) % MAX_OUTGOING;

        OSUnlockMutex(&gQueueMutex);
        return true;
    }

    // send exactly len bytes (blocking, but robust to partial writes / retryable returns)
    static bool SendAll(s32 fd, const u8* buf, int len)
    {
        int off = 0;
        while (off < len) {
            int n = Mynet_write(fd, buf + off, len - off);
            if (n > 0) { off += n; continue; }
            if (n == 0) return false;          // peer closed
            if (IsRetryableSockRet(n)) {        // "would block" style return in your wrapper
                wii::os::OSYieldThread();
                continue;
            }
            wii::os::OSReport("SendAll write failed ret=%d\n", n);
            return false;
        }
        return true;
    }

    // receive exactly len bytes (blocking, but robust to retryable returns)
    static bool RecvAll(s32 fd, u8* buf, int len)
    {
        int off = 0;
        while (off < len) {
            int n = Mynet_read(fd, buf + off, len - off);
            if (n > 0) { off += n; continue; }
            if (n == 0) return false;          // peer closed
            if (IsRetryableSockRet(n)) {
                wii::os::OSYieldThread();
                continue;
            }
            wii::os::OSReport("RecvAll read failed ret=%d\n", n);
            return false;
        }
        return true;
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

    bool enqueuePacket(u16 cmdId, const void* payload, u16 payloadLen)
    {
        OSLockMutex(&gQueueMutex);

        int head = goutgoingHead;
        int tail = goutgoingTail;
        int next = (head + 1) % MAX_OUTGOING;

        if (next == tail) {
            OSUnlockMutex(&gQueueMutex);
            return false;
        }

        OutgoingPacket* p = &outgoingPackets[head];
        
        u16 len = payloadLen;

        if (len > sizeof(p->data))
        {
            len = sizeof(p->data);
        }

        p->cmdID = cmdId;
        p->length = len;

        if (len > 0)
        {
            msl::string::memcpy(p->data, payload, len);
        }

        goutgoingHead = next;

        OSUnlockMutex(&gQueueMutex);
        return true;
    }

    static void senderLoop(u32 param)
    {
        (void)param;

        u8 txBuff[BUFSIZE];

        while (1) {
            s32 fd;
            if (!GetConnection(&fd)) {
                wii::os::OSYieldThread();
                continue;
            }

            OutgoingPacket pkt;
            if (!DequeueOutgoing(&pkt)) {
                // nothing to send right now
                wii::os::OSYieldThread();
                continue;
            }

            u16 frameLen = (u16)(pkt.length + 4);
            if (frameLen > BUFSIZE) {
                wii::os::OSReport("Outgoing too big len=%u\n", frameLen);
                continue;
            }

            // Build frame
            msl::string::memcpy(txBuff + 0, &pkt.cmdID, 2);
            msl::string::memcpy(txBuff + 2, &frameLen, 2);
            if (pkt.length) {
                msl::string::memcpy(txBuff + 4, pkt.data, pkt.length);
            }

            // Send it
            if (!SendAll(fd, txBuff, frameLen)) {
                // If send fails, drop connection so recv thread can cleanly close/reaccept
                wii::os::OSReport("senderLoop: send failed; clearing connection\n");
                ClearConnection();
                wii::os::OSYieldThread();
            }
        }
    }

    void receiverLoop(u32 param)
    {
        (void)param;

        wii::os::OSReport("Initializing network...\n");
        s32 res = initializeNetwork();
        if (res < 0) {
            wii::os::OSReport("Initializing network failed. %d\n", res);
            return;
        }
        wii::os::OSReport("Network initialized successfully.\n");

        wii::os::OSReport("initializing commands..    ");
        mod::initCommands();
        wii::os::OSReport("commands initialized\n");

        int listenfd = Mynet_socket(AF_INET, SOCK_STREAM, 0);
        if (listenfd < 0) {
            wii::os::OSReport("socket() failed %d\n", listenfd);
            return;
        }

        struct sockaddr_in serv_addr;
        msl::string::memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        serv_addr.sin_port = htons(PORT);

        if (Mynet_bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) {
            wii::os::OSReport("bind() failed\n");
            Mynet_close(listenfd);
            return;
        }
        if (Mynet_listen(listenfd, 10)) {
            wii::os::OSReport("listen() failed\n");
            Mynet_close(listenfd);
            return;
        }

        u8 recvBuff[BUFSIZE];
        u8 sendScratch[BUFSIZE]; // used ONLY for parseAndExecute output

        while (1)
        {
            wii::os::OSReport("waiting for connection..\n");

            u32 addrlen = sizeof(serv_addr);
            s32 connfd = -1;
            while (connfd < 0) {
                wii::os::OSYieldThread();
                connfd = Mynet_accept(listenfd, (struct sockaddr*)&serv_addr, &addrlen);
                if (connfd < 0) {
                    if (IsRetryableSockRet(connfd)) continue;
                    // keep waiting (optional log)
                    continue;
                }
            }

            wii::os::OSReport("client connected fd=%d\n", connfd);

            // reset queue on new connection (optional but nice)
            OSLockMutex(&gQueueMutex);
            goutgoingHead = 0;
            goutgoingTail = 0;
            OSUnlockMutex(&gQueueMutex);

            // publish connection to sender thread
            SetConnection(connfd);

            // hello
            const char* hello = "hello from Wii";
            enqueuePacket(0x1001, hello, (u16)(msl::string::strlen(hello) + 1));

            bool connected = true;

            while (connected)
            {
                // Read 4-byte header
                if (!RecvAll(connfd, recvBuff, 4)) {
                    connected = false;
                    break;
                }

                u16 cmdId = 0;
                u16 packetLen = 0;
                msl::string::memcpy(&cmdId,    recvBuff + 0, 2);
                msl::string::memcpy(&packetLen, recvBuff + 2, 2);

                if (packetLen < 4 || packetLen > BUFSIZE) {
                    wii::os::OSReport("Bad packet length: %u\n", packetLen);
                    connected = false;
                    break;
                }

                // Read the rest of the packet
                int bodyLen = (int)packetLen - 4;
                if (bodyLen > 0) {
                    if (!RecvAll(connfd, recvBuff + 4, bodyLen)) {
                        connected = false;
                        break;
                    }
                }

                // Execute command; write response payload into sendScratch+4
                auto* commandManager = mod::CommandManager::Instance();
                s32 outSize = commandManager->parseAndExecute(
                    recvBuff,
                    packetLen,
                    sendScratch + 4,
                    BUFSIZE - 4
                );

                if (outSize < 0) outSize = 0;
                if (outSize > (BUFSIZE - 4)) outSize = (BUFSIZE - 4);

                // enqueue response (sender thread will actually write it)
                enqueuePacket(cmdId, sendScratch + 4, (u16)outSize);
            }

            wii::os::OSReport("Client disconnected..\n");

            // make sender stop using fd BEFORE close
            ClearConnection();

            Mynet_close(connfd);
        }
    }

    static inline u8* Align32(u8* p) {
        return (u8*)(((uintptr_t)p) & ~((uintptr_t)0x1F));
    }

    void init()
    {
        InitQueueMutexOnce();
        InitConnMutexOnce();

        u8* sendSp = Align32(gSendStack + STACK_SIZE);
        u8* recvSp = Align32(stack      + STACK_SIZE);

        int ok;

        ok = wii::os::OSCreateThread(&gSendThread, (wii::os::ThreadFunc*)senderLoop,
                                    0, sendSp, STACK_SIZE, 24, 1);
        wii::os::OSReport("Create sender ok=%d\n", ok);
        if (!ok) return;
        wii::os::OSResumeThread(&gSendThread);
        wii::os::OSReport("Resumed sender\n");

        ok = wii::os::OSCreateThread(&thread, (wii::os::ThreadFunc*)receiverLoop,
                                    0, recvSp, STACK_SIZE, 16, 1);
        wii::os::OSReport("Create receiver ok=%d\n", ok);
        if (!ok) return;
        wii::os::OSResumeThread(&thread);
        wii::os::OSReport("Resumed receiver\n");
    }
}
