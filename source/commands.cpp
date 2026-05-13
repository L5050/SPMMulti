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

/*inline bool isWithinMem1Range(s32 ptr) {
    return (ptr >= 0x80000000 && ptr <= 0x817fffff);
}*/

/*inline bool isValidGamePtr(void* ptr)
{
    u32 addr = (u32)ptr;

    return
        (addr >= 0x80000000 && addr < 0x81800000) ||  // MEM1
        (addr >= 0x90000000 && addr < 0x94000000);    // MEM2
}*/

//Test Command
    COMMAND(CMD_MULTI, multi, "Set up blank command for potential use in updating multiplayer state", 
        {
            return msl::stdio::snprintf(
                (char*)response,
                responseSize,
                "Hello Python"
            );
        });


/*EVT_DEFINE_USER_FUNC(evt_deref) {
    s32 addr = spm::evtmgr_cmd::evtGetValue(evt, evt->pCurData[0]);
    SPM_ASSERT(isWithinMem1Range(addr), "evt_deref error");
    s32* ptr = reinterpret_cast<s32*>(addr);
    spm::evtmgr_cmd::evtSetValue(evt, evt->pCurData[1], *ptr);
    return EVT_RET_CONTINUE;
}*/

}
