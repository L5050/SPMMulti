#pragma once
#include "evt_cmd.h"
namespace mod {

#define MOD_VERSION "SPM-Multi"

extern bool gIsDolphin;

EVT_DECLARE(evt_spawn_players)

s32 roundi(f32 x);
void main();

}
