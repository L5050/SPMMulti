#include "commands.h"
#include "stack.hh"
#include <spm/evt_mario.h>
#include <spm/evt_msg.h>
#include <spm/evt_mario.h>
#include <spm/evt_item.h>
#include <spm/itemdrv.h>
#include <spm/evtmgr.h>
#include <spm/system.h>
#include <spm/mario.h>
#include <spm/spmario.h>
#include <wii/os.h>
#include <msl/math.h>
#include <msl/stdio.h>

namespace mod {
u32 itemMax = 0;
char item_name[11];
static u32 * s_lastItemIdx = (u32 *)&spm::spmario::gp->gsw[1000];
static Stack<s32> itemStack;

inline bool isWithinMem1Range(s32 ptr) {
    return (ptr >= 0x80000000 && ptr <= 0x817fffff);
}

s32 evt_item_entry_autoname(spm::evtmgr::EvtEntry *evtEntry, bool firstRun)
{
  spm::evtmgr::EvtVar *args = (spm::evtmgr::EvtVar *)evtEntry->pCurData;
  msl::stdio::sprintf(item_name, "i_%d_%d", *s_lastItemIdx, itemMax);
  itemMax++;
  spm::evtmgr_cmd::evtSetValue(evtEntry, args[0], item_name);
  spm::evt_item::evt_item_entry(evtEntry, firstRun);

  spm::evtmgr_cmd::evtSetValue(evtEntry, args[0], spm::itemdrv::itemNameToPtr(item_name)->name);
  return 2;
}

EVT_DECLARE_USER_FUNC(evt_item_entry_autoname, -1)

s32 add_to_gswf_stack(spm::evtmgr::EvtEntry *evtEntry, bool firstRun)
{
  spm::evtmgr::EvtVar *args = (spm::evtmgr::EvtVar *)evtEntry->pCurData;
  const char * name = (const char*)spm::evtmgr_cmd::evtGetValue(evtEntry, args[0]);
  spm::itemdrv::ItemEntry * item = spm::itemdrv::itemNameToPtr(name);
  spm::evtmgr::EvtVar gswf = abs(item->switchNumber);
  gswf -= (abs(GSWF(0)));
  gswf = abs(gswf);
  itemStack.push(gswf);
  wii::os::OSReport("GSWF: %d\n", gswf);
  return 2;
}

void addToGswfStack(spm::itemdrv::ItemEntry * item)
{
  spm::evtmgr::EvtVar gswf = abs(item->switchNumber);
  gswf -= (abs(GSWF(0)));
  gswf = abs(gswf);
  itemStack.push(gswf);
  wii::os::OSReport("GSWF: %d\n", gswf);
  return;
}

EVT_BEGIN(give_ap_item)
USER_FUNC(spm::evt_mario::evt_mario_get_pos, LW(5), LW(6), LW(7))
USER_FUNC(evt_item_entry_autoname, LW(8), LW(0), LW(5), LW(6), LW(7), 0, 0, 0, 0, 0)
USER_FUNC(spm::evt_item::evt_item_flag_onoff, 1, LW(8), 8)
USER_FUNC(spm::evt_item::evt_item_wait_collected, LW(8))
USER_FUNC(spm::evt_mario::evt_mario_key_on)
RETURN()
EVT_END()

COMMAND(CMD_ITEM, item, "Gives mario an item. (item itemId)", 1, {
    wii::os::OSReport("itemId %s\n", args[0]);
    s32 itemId = strtoul(args[0], NULL, 10);
    wii::os::OSReport("itemId %d\n", itemId);
    spm::evtmgr::EvtEntry* evt = spm::evtmgr::evtEntry(give_ap_item, 0, 0);
    evt->lw[0] = itemId;

    return 1;
})

COMMAND(CMD_IDX, idx, "Sets the current item index. (idx index)", 1, {
    wii::os::OSReport("idx %s\n", args[0]);
    s32 idx = strtoul(args[0], NULL, 10);
    wii::os::OSReport("idx %d\n", idx);

    *s_lastItemIdx = idx;

    return 1;
})

COMMAND(CMD_rIDX, ridx, "Reads the current item index. (ridx)", 0, {
    wii::os::OSReport("ridx\n");

    u32 idx = *s_lastItemIdx;

    msl::string::memcpy((void*)response, &idx, sizeof(u32));
    return sizeof(u32);
})

COMMAND(CMD_rBUSY, rbusy, "Reads whether Mario is busy (i.e., loading zone/item-acceptance menu). (rbusy)", 0, {
    wii::os::OSReport("rbusy\n");

    bool busy = spm::mario::marioKeyOffChk();

    msl::string::memcpy((void*)response, &busy, sizeof(bool));
    return sizeof(bool);
})

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

    u16 itemState;
    // DUPLICATE / OUT-OF-ORDER GUARD
    if (idx != (*s_lastItemIdx + 1)) {
        itemState = 8; // Error code for duplicate / out-of-order
        msl::string::memcpy((void*)response, &itemState, sizeof(u16));
        return sizeof(u16);
    
    } else {
    if (spm::mario::marioKeyOffChk())
    {
        itemState = 9; // Error code for busy
        msl::string::memcpy((void*)response, &itemState, sizeof(u16));
        return sizeof(u16);
    }

        // Accept and advance
        *s_lastItemIdx = idx;

        spm::mario::marioKeyOff();
        spm::evtmgr::EvtEntry* evt = spm::evtmgr::evtEntry(give_ap_item, 0, 0);
        evt->lw[0] = (s32)itemId;

        if (responseSize >= sizeof(u16)) {
            itemState = 1; // Success
            msl::string::memcpy((void*)response, &itemState, sizeof(u16));
            return sizeof(u16);
        }
        itemState = 0; // response too big
        msl::string::memcpy((void*)response, &itemState, sizeof(u16));
        return sizeof(u16);
    } 
}

u32 handleIdxBinary(
    const u8* payload,
    size_t payloadLen,
    u8* response,
    size_t responseSize
) {
    if (payloadLen < sizeof(u32)) {
        wii::os::OSReport("CMD_IDX: payload too small (%zu)\n", payloadLen);
        return 0;
    }

    u32 idx;
    msl::string::memcpy(&idx, payload + 0, sizeof(u32));
    *s_lastItemIdx = idx;

    wii::os::OSReport("CMD_IDX: received idx=%u\n", idx);
    char msg[128];
    msl::stdio::snprintf(
            msg,
            sizeof(msg),
            "CMD_IDX: received idx=%u\n",
            idx
        );

    u32 len = strlen(msg);
    memcpy(response, &len, sizeof(len));
    memcpy(response + sizeof(len), msg, len);
    return sizeof(len) + len;
}

u32 readMemoryIdx(
    u8* response,
    size_t responseSize
) {
    wii::os::OSReport("CMD_rIDX: reading idx\n");

    u32 idx = *s_lastItemIdx;

    msl::string::memcpy((void*)response, &idx, sizeof(u32));
    return sizeof(u32);
}

u32 readMemoryBusy(
    u8* response,
    size_t responseSize
) {
    wii::os::OSReport("CMD_rBUSY: reading busy\n");

    bool busy = spm::mario::marioKeyOffChk();

    msl::string::memcpy((void*)response, &busy, sizeof(bool));
    return sizeof(bool);
}

EVT_DEFINE_USER_FUNC(evt_deref) {
    s32 addr = spm::evtmgr_cmd::evtGetValue(evt, evt->pCurData[0]);
    SPM_ASSERT(isWithinMem1Range(addr), "evt_deref error");
    s32* ptr = reinterpret_cast<s32*>(addr);
    spm::evtmgr_cmd::evtSetValue(evt, evt->pCurData[1], *ptr);
    return EVT_RET_CONTINUE;
}

}
