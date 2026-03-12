#include "commands.h"
#include "stack.hh"
#include <spm/evt_mario.h>
#include <spm/evt_msg.h>
#include <spm/evt_item.h>
#include <spm/mapdrv.h>
#include <spm/itemdrv.h>
#include <spm/evtmgr.h>
#include <spm/effdrv.h>
#include "mod.h"
#include <spm/system.h>
#include <spm/mario.h>
#include <spm/spmario.h>
#include <wii/os.h>
#include <wii/mtx.h>
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

//param helper
struct Params
{
    int ints[16];
    float floats[16];
    double doubles[16];
    void* pointers[16];

    const char* strings[16];
    u16 stringLens[16];

    int nInts;
    int nFloats;
    int nDoubles;
    int nStrings;
    int nPtrs;

    bool valid;
};

Params payloadProcess(const u8* payload, size_t payloadLen)
{
    Params params;

    params.nInts = 0;
    params.nFloats = 0;
    params.nDoubles = 0;
    params.nStrings = 0;
    params.nPtrs = 0;
    params.valid = true;

    const u8* p = payload;
    const u8* end = payload + payloadLen;

    while(p < end)
    {
        char t = *p++;

        switch(t)
        {
            case 'i':
            {
                if(p+4 > end) { params.valid=false; return params; }

                params.ints[params.nInts++] =
                (p[0] << 24) |
                (p[1] << 16) |
                (p[2] << 8)  |
                (p[3]);

                p += 4;
                break;
            }

            case 'f':
            {
                if(p+4 > end) { params.valid=false; return params; }

                u32 v =
                (p[0] << 24) |
                (p[1] << 16) |
                (p[2] << 8)  |
                (p[3]);

                params.floats[params.nFloats++] =
                    *(float*)&v;

                p += 4;
                break;
            }

            case 'd':
            {
                if(p+8 > end) { params.valid=false; return params; }

                u64 v =
                ((u64)p[0] << 56) |
                ((u64)p[1] << 48) |
                ((u64)p[2] << 40) |
                ((u64)p[3] << 32) |
                ((u64)p[4] << 24) |
                ((u64)p[5] << 16) |
                ((u64)p[6] << 8)  |
                ((u64)p[7]);

                params.doubles[params.nDoubles++] =
                    *(double*)&v;

                p += 8;
                break;
            }

            case 's':
            {
                if(p+2 > end) { params.valid=false; return params; }

                u16 len = *(u16*)p;
                p += 2;

                if(p+len > end) { params.valid=false; return params; }

                params.strings[params.nStrings] =
                    (char*)p;

                params.stringLens[params.nStrings] =
                    len;

                params.nStrings++;

                p += len;

                break;
            }

            default:
            {
                params.valid = false;
                return params;
            }
        }
    }

    return params;
}

//Symbols

//Help Command
    COMMAND(CMD_HELP, help, "Returns the arg(s) for command id in Payload. (help categoryId commandId)", 
        {
            if (payloadLen < 2) {
                wii::os::OSReport("CMD_HELP: payload too small (%zu)\n", payloadLen);
                return 0;
            }

            u8 categoryId = payload[0];
            u8 commandId = payload[1];

            CommandId fullId = (CommandId)((categoryId << 8) | commandId);

            auto cmd = CommandManager::Instance()->findCommandById(fullId);

            if (!cmd) {
                return msl::stdio::snprintf(
                    (char*)response,
                    responseSize,
                    "Unknown command %d:%d",
                    categoryId,
                    commandId
                );
            }

            return msl::stdio::snprintf(
                (char*)response,
                responseSize,
                "%s",
                cmd->getHelpMsg()
            );
        });

//Read Commands
    
    COMMAND(CMD_rIDX, ridx, "Reads the current item index. (ridx)", 
        {
            wii::os::OSReport("CMD_rIDX: reading idx\n");

            u32 idx = *s_lastItemIdx;

            msl::string::memcpy((void*)response, &idx, sizeof(u32));
            return sizeof(u32);
        });

    COMMAND(CMD_rBUSY, rbusy, "Reads whether Mario is busy (i.e., loading zone/item-acceptance menu). (rbusy)", 
        {
            wii::os::OSReport("CMD_rBUSY: reading busy\n");

            bool busy = spm::mario::marioKeyOffChk();

            u32 busyValue = busy ? 1 : 0;
            msl::string::memcpy((void*)response, &busyValue, sizeof(u32));
            return sizeof(u32);
        });

//Base Commands
    COMMAND(CMD_ITEM, item, "Gives mario an item. (item itemId)", 
    {
        Params params =
        payloadProcess(payload, payloadLen);

        if(!params.valid || params.nInts < 1) 
            {
                return msl::stdio::snprintf(
                    (char*)response,
                    responseSize,
                    "%s",
                    item.getHelpMsg()
                );
            };

        if (spm::mario::marioKeyOffChk())
            {
                return msl::stdio::snprintf(
                    (char*)response,
                    responseSize,
                    "Mario busy"
                );
            }

        spm::mario::marioKeyOff();

        int itemId = params.ints[0];
        wii::os::OSReport("CMD_ITEM: giving item id=%u\n", itemId);
        spm::evtmgr::EvtEntry* evt = spm::evtmgr::evtEntry(give_ap_item, 0, 0);
        if (!evt)
            {
                return msl::stdio::snprintf(
                    (char*)response,
                    responseSize,
                    "evtEntry failed"
                );
            }

        evt->lw[0] = (u16)itemId;

        char msg[128];
        return msl::stdio::snprintf(
            (char*)response,
            responseSize,
            "CMD_ITEM: received item id=%u\n",
            itemId
        );
    });

    COMMAND(CMD_IDX, idx, "Sets the current item index. (idx index)", 
        {
            Params params =
            payloadProcess(payload, payloadLen);
            if(!params.valid || params.nInts < 1) 
                {
                    return msl::stdio::snprintf(
                        (char*)response,
                        responseSize,
                        "%s",
                        idx.getHelpMsg()
                    );
                };

            u32 index = params.ints[0];
            wii::os::OSReport("CMD_IDX: setting idx to %u\n", index);
            *s_lastItemIdx = index;


            char msg[128];
            return msl::stdio::snprintf(
                (char*)response,
                responseSize,
                "CMD_IDX: received idx=%u\n",
                index
            );
        });

EVT_DEFINE_USER_FUNC(evt_deref) {
    s32 addr = spm::evtmgr_cmd::evtGetValue(evt, evt->pCurData[0]);
    SPM_ASSERT(isWithinMem1Range(addr), "evt_deref error");
    s32* ptr = reinterpret_cast<s32*>(addr);
    spm::evtmgr_cmd::evtSetValue(evt, evt->pCurData[1], *ptr);
    return EVT_RET_CONTINUE;
}

}
