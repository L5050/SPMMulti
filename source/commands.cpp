#include "commands.h"
#include <spm/evt_mario.h>
#include <spm/evt_msg.h>
#include <spm/evt_mario.h>
#include <spm/evt_item.h>
#include <spm/evtmgr.h>
#include <spm/system.h>
#include <spm/spmario.h>
#include <wii/os.h>
#include <msl/stdio.h>

namespace mod {

inline bool isWithinMem1Range(s32 ptr) {
    return (ptr >= 0x80000000 && ptr <= 0x817fffff);
}

/*COMMAND(read, "Reads memory from the console. (read address n)", 2, {
    u32 ptr = strtoul(args[0], NULL, 16);
    u32 size = strtoul(args[1], NULL, 10);
    if (size > responseSize) {
        msl::stdio::snprintf((char*)response, responseSize, "Error: Requested more bytes (%d) than the allocated buffer's size (%d).\n", size, responseSize);
        return (u32)msl::string::strlen((char*)response);
    }
    if (ptr < 0x80000000 || ptr + size > 0x817fffff) {
        msl::stdio::snprintf((char*)response, responseSize, "Error: Address (%x) or end of buffer (%x) outside of MEM1's range (0x80000000 - 0x817fffff).\n", ptr, ptr + size);
        return (u32)msl::string::strlen((char*)response);
    }
    msl::string::memcpy(response, (void*)ptr, size);
    return size;
})

COMMAND(write, "Writes memory to the console. (write address base64_encoded_bytearray decoded_size)", 3, {
    u32 dest = strtoul(args[0], NULL, 16);
    const char* b64data = args[1];
    s32 decodedLen = strtoul(args[2], NULL, 10);

    if (!isValidBase64(b64data, decodedLen)) {
        msl::stdio::snprintf((char*)response, responseSize, "Error: Invalid Base64 data.\n");
        return (u32)msl::string::strlen((char*)response);
    }
    if (!isWithinMem1Range(dest) || !isWithinMem1Range(dest+decodedLen)) {
        msl::stdio::snprintf((char*)response, responseSize, "Error: Address (%x) or end of buffer (%x) outside of MEM1's range (0x80000000 - 0x817fffff).\n", dest, dest + decodedLen);
        return (u32)msl::string::strlen((char*)response);
    }
    char* decodedData = new char[decodedLen];
    Base64decode(decodedData, b64data);
    msl::string::memcpy((void*)dest, decodedData, decodedLen);
    delete[] decodedData;

    msl::string::memcpy((void*)response, &decodedLen, sizeof(s32));
    return (u32)sizeof(s32);
})*/

EVT_BEGIN(give_ap_item)
USER_FUNC(spm::evt_mario::evt_mario_get_pos, LW(5), LW(6), LW(7))
USER_FUNC(spm::evt_item::evt_item_entry, PTR("ap_get"), LW(0), LW(5), LW(6), LW(7), 0, 0, 0, 0, 0)
USER_FUNC(spm::evt_item::evt_item_flag_onoff, 1, PTR("ap_get"), 8)
RETURN()
EVT_END()

/*EVT_BEGIN(msgbox_cmd)
    USER_FUNC(spm::evt_mario::evt_mario_key_off, 1)
    USER_FUNC(spm::evt_msg::evt_msg_print, 1, LW(0), 0, 0)
    USER_FUNC(spm::evt_msg::evt_msg_continue)
    USER_FUNC(evt_post_msgbox, LW(0))
    USER_FUNC(spm::evt_mario::evt_mario_key_on)
    RETURN()
EVT_END()
EVT_BEGIN(fwd_msgbox_cmd)
    RUN_EVT(msgbox_cmd)
    RETURN()
EVT_END()*/

/*COMMAND(msgbox, "Displays a message box on the screen. (msgbox base64_encoded_string size)", 2, {
    const char* b64data = args[0];
    s32 msgboxTextLen = strtoul(args[1], NULL, 10);

    if (!isValidBase64(b64data, msgboxTextLen)) {
        msl::stdio::snprintf((char*)response, responseSize, "Error: Invalid Base64 data.\n");
        return msl::string::strlen((char*)response);
    }

    char* msgboxText = new char[msgboxTextLen + 1];
    Base64decode(msgboxText, b64data);
    msgboxText[msgboxTextLen] = '\0';
    spm::evtmgr::EvtEntry* evt = spm::evtmgr::evtEntry(fwd_msgbox_cmd, 0, 0);
    evt->lw[0] = msgboxText;

    msl::string::memcpy((void*)response, &msgboxTextLen, sizeof(s32));
    return sizeof(s32);
})*/

COMMAND(CMD_ITEM, item, "Gives mario an item. (item itemId)", 1, {
    wii::os::OSReport("itemId %s\n", args[0]);
    s32 itemId = strtoul(args[0], NULL, 10);
    wii::os::OSReport("itemId %d\n", itemId);
    spm::evtmgr::EvtEntry* evt = spm::evtmgr::evtEntry(give_ap_item, 0, 0);
    evt->lw[0] = itemId;

    return 1;
})

/*EVT_DEFINE_USER_FUNC(evt_post_msgbox) {
    char* textPtr = reinterpret_cast<char*>(spm::evtmgr_cmd::evtGetValue(evt, evt->pCurData[0]));
    delete[] textPtr;
    return EVT_RET_CONTINUE;
}*/

static u32 * s_lastItemIdx = (u32 *)&spm::spmario::gp->gsw[1000];

u32 handleItemBinary(
    const u8* payload,
    size_t payloadLen,
    u8* response,
    size_t responseSize
) {
    if (payloadLen < (sizeof(u32) + sizeof(u16))) {
        wii::os::OSReport("CMD_ITEM: payload too small (%zu)\n", payloadLen);
        return 0;
    }

    u32 idx;
    u16 itemId;
    msl::string::memcpy(&idx, payload + 0, sizeof(u32));
    msl::string::memcpy(&itemId, payload + sizeof(u32), sizeof(u16));

    // DUPLICATE / OUT-OF-ORDER GUARD
    if (idx != (*s_lastItemIdx + 1)) {
        wii::os::OSReport("CMD_ITEM: ignored idx=%u (last=%u)\n", idx, *s_lastItemIdx);

        if (responseSize >= sizeof(u32)) {
            u32 ignored = 0;
            msl::string::memcpy(response, &ignored, sizeof(u32));
            return sizeof(u32);
        }
        return 0;
    } else {

        // Accept and advance
        *s_lastItemIdx = idx;

        wii::os::OSReport("CMD_ITEM: accept idx=%u itemId=%u\n", idx, itemId);

        spm::evtmgr::EvtEntry* evt = spm::evtmgr::evtEntry(give_ap_item, 0, 0);
        evt->lw[0] = (s32)itemId;

        if (responseSize >= sizeof(u32)) {
            u32 ok = 1;
            msl::string::memcpy(response, &ok, sizeof(u32));
            return sizeof(u32);
        }
        return 0;
} 
}

EVT_DEFINE_USER_FUNC(evt_deref) {
    s32 addr = spm::evtmgr_cmd::evtGetValue(evt, evt->pCurData[0]);
    assert(isWithinMem1Range(addr), "evt_deref error");
    s32* ptr = reinterpret_cast<s32*>(addr);
    spm::evtmgr_cmd::evtSetValue(evt, evt->pCurData[1], *ptr);
    return EVT_RET_CONTINUE;
}

}
