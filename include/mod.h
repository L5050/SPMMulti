#pragma once
#include "evt_cmd.h"
#include <spm/effdrv.h>
namespace mod {

#define MOD_VERSION "SPM-Multi"

extern bool gIsDolphin;

extern int main();

#define MAX_CHECK_LIST 2

struct ItemCheckList
{
  s32 gswfIndex = 0;
  s32 itemid = 0;
};

}
