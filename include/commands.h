#pragma once
#include "common.h"
#include "base64.h"
#include "commandmanager.h"
#include "evt_cmd.h"
#include <spm/itemdrv.h>
#include <msl/math.h>
#include <msl/string.h>
#include <stdlib.h>

namespace mod {

u32 handleHelpBinary(const u16 commandId, const u8* payload, size_t payloadLen, u8* response, size_t responseSize);

u32 handleReadBinary(const u16 commandId, u8* response, size_t responseSize);
u32 handleBaseBinary(const u16 commandId, const u8* payload, size_t payloadLen, u8* response, size_t responseSize);

#define COMMAND(id, name, description, code) \
    Command name(id, #name, description, [](const u8* payload, size_t payloadLen, u8* response, size_t responseSize) -> u32 code);

/*extern Command read;
extern Command write;

extern Command msgbox;*/

//Special case Help command
extern Command help; // 0xFFFF

// Read Commands
extern Command ridx; // 0x0000
extern Command rbusy; // 0x0001

// Base Commands
extern Command item; // 0x0100
extern Command idx; // 0x0101

/*EVT_DECLARE(msgbox_cmd)
EVT_DECLARE(fwd_msgbox_cmd)
EVT_DECLARE_USER_FUNC(evt_post_msgbox, 1)*/
// deref(s32* ptr, s32* outvar);
EVT_DECLARE_USER_FUNC(evt_deref, 2)
EVT_DECLARE_USER_FUNC(add_to_gswf_stack, 1)
void addToGswfStack(spm::itemdrv::ItemEntry * item);

}