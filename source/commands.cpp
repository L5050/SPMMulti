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
#include <spm/pausewin.h>
#include <wii/os.h>
#include <wii/mtx.h>
#include <msl/math.h>
#include <msl/stdio.h>

namespace mod {
u32 itemMax = 0;
char item_name[11];
static u32 * s_lastItemIdx = (u32 *)&spm::spmario::gp->gsw[1000];
static Stack<s32> itemStack;

inline bool isWithinMem1Range(u32 ptr) {
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



COMMAND(ap, "N/A", 
    {
        return msl::stdio::snprintf(
            (char*)response,
            responseSize,
            "%s",
            payload
        );
    });

EVT_DEFINE_USER_FUNC(evt_deref) {
    s32 addr = spm::evtmgr_cmd::evtGetValue(evt, evt->pCurData[0]);
    SPM_ASSERT(isWithinMem1Range(addr), "evt_deref error");
    void* ptr = reinterpret_cast<s32*>(addr);
    spm::evtmgr_cmd::evtSetValue(evt, evt->pCurData[1], ptr);
    return EVT_RET_CONTINUE;
}

}
