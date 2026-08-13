#pragma once
#include "evt_cmd.h"
namespace mod {

#define MOD_VERSION "SPM-Multi"

extern bool gIsDolphin;

void main();

#define MAX_CHECK_LIST 2

typedef struct ItemCheckList
{
  s32 gswfIndex = 0;
  s32 itemid = 0;
};

}
