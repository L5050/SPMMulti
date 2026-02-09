#pragma once
#include "common.h"
#include "base64.h"
#include "commandmanager.h"
#include "evt_cmd.h"
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <spm/itemdrv.h>
#include <msl/math.h>
#include <msl/string.h>
#include <stdlib.h>

#define ANY_ARGC -1

namespace mod {

u32 handleItemBinary(const u8* payload, size_t payloadLen, u8* response, size_t responseSize);
u32 handleIdxBinary(const u8* payload, size_t payloadLen, u8* response, size_t responseSize);
u32 readMemoryIdx(u8* response, size_t responseSize);
u32 readMemoryBusy(u8* response, size_t responseSize);

#define COMMAND(id, name, description, argc, code) \
    Command name(id, #name, description, argc, [](eastl::vector<const char*> &args, u8* response, size_t responseSize) -> u32 code);

/*extern Command read;
extern Command write;

extern Command msgbox;*/
extern Command item;
extern Command idx;
extern Command ridx;
extern Command rbusy;
/*EVT_DECLARE(msgbox_cmd)
EVT_DECLARE(fwd_msgbox_cmd)
EVT_DECLARE_USER_FUNC(evt_post_msgbox, 1)*/
// deref(s32* ptr, s32* outvar);
EVT_DECLARE_USER_FUNC(evt_deref, 2)
EVT_DECLARE_USER_FUNC(add_to_gswf_stack, 1)
void addToGswfStack(spm::itemdrv::ItemEntry * item);

}