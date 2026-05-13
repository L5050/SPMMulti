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

#define COMMAND(id, name, description, code) \
    Command name(id, #name, description, [](const u8* payload, size_t payloadLen, u8* response, size_t responseSize) -> u32 code)

/*extern Command read;
extern Command write;

extern Command msgbox;*/

//Test command
extern Command multi; // 0x1999

/*EVT_DECLARE(msgbox_cmd)
EVT_DECLARE(fwd_msgbox_cmd)
EVT_DECLARE_USER_FUNC(evt_post_msgbox, 1)*/
// deref(s32* ptr, s32* outvar);
//EVT_DECLARE_USER_FUNC(evt_deref, 2)
}