#include "mod.h"
#include "commandmanager.h"
#include "evtpatch.h"
#include "patch.h"
#include "msgpatch.h"
#include "netmemoryaccess.h"
#include "network.h"

#include <spm/rel/aa1_01.h>
#include <spm/fontmgr.h>
#include <spm/seqdrv.h>
#include <spm/seqdef.h>
#include <spm/item_data.h>
#include <spm/mario_pouch.h>
#include <spm/evt_pouch.h>
#include <spm/evt_seq.h>
#include <spm/map_data.h>
#include <spm/spmario.h>
#include <wii/os/OSError.h>
#include <msl/stdio.h>

namespace mod {

/*
    Title Screen Custom Text
    Prints "SPM Rel Loader" at the top of the title screen
*/

static spm::seqdef::SeqFunc *seq_titleMainReal;
static void seq_titleMainOverride(spm::seqdrv::SeqWork *wp)
{
    wii::gx::GXColor _colour {0, 255, 0, 255};
    f32 scale = 0.8f;
    char msg[128];
    u32 ip = Mynet_gethostip();
    msl::stdio::snprintf(msg, 128, "%d.%d.%d.%d\n", ip >> 24 & 0xff, ip >> 16 & 0xff, ip >> 8 & 0xff, ip & 0xff);
    spm::fontmgr::FontDrawStart();
    spm::fontmgr::FontDrawEdge();
    spm::fontmgr::FontDrawColor(&_colour);
    spm::fontmgr::FontDrawScale(scale);
    spm::fontmgr::FontDrawNoiseOff();
    spm::fontmgr::FontDrawRainbowColorOff();
    f32 x = -((spm::fontmgr::FontGetMessageWidth(msg) * scale) / 2);
    spm::fontmgr::FontDrawString(x, 200.0f, msg);
    seq_titleMainReal(wp);
}
static void titleScreenCustomTextPatch()
{
    seq_titleMainReal = spm::seqdef::seq_data[spm::seqdrv::SEQ_TITLE].main;
    spm::seqdef::seq_data[spm::seqdrv::SEQ_TITLE].main = &seq_titleMainOverride;
}
  bool( * pouchAddItem)(s32 itemId);

  bool new_pouchAddItem(s32 itemId)
  {
    if (itemId == 45)
    {
      return true;
    }
    return pouchAddItem(itemId);
  }

  EVT_BEGIN(insertNop)
    SET(LW(0), LW(0))
  RETURN_FROM_CALL()
  
  EVT_BEGIN(gn4)
    SET(GSW(0), 215)
  RETURN_FROM_CALL()
  
  EVT_BEGIN(gn2)
    SET(GSW(0), 189)
  RETURN_FROM_CALL()
  
  EVT_BEGIN(sp2)
    SET(GSW(0), 142)
  RETURN_FROM_CALL()
  
  EVT_BEGIN(ta2)
    SET(GSW(0), 107)
  RETURN_FROM_CALL()
  
  EVT_BEGIN(ta4)
    SET(GSW(0), 120)
  RETURN_FROM_CALL()
  
  EVT_BEGIN(ls1)
    SET(GSW(0), 358)
  RETURN_FROM_CALL()

  EVT_BEGIN(he2_mi1)
    SET(GSW(0), 20)
  RETURN_FROM_CALL()
  
  EVT_BEGIN(he1)
    SET(GSW(0), 17)
  RETURN_FROM_CALL()


  // Dialogue to determine quickstart or no
  EVT_BEGIN(determine_quickstart)
  SET(GSW(0), 17)
  USER_FUNC(spm::evt_pouch::evt_pouch_add_item, 50)
  USER_FUNC(spm::evt_pouch::evt_pouch_add_item, 0x0D9)
  USER_FUNC(spm::evt_pouch::evt_pouch_add_item, 0x0DA)
  USER_FUNC(spm::evt_pouch::evt_pouch_add_item, 0x0DB)
  // USER_FUNC(spm::evt_pouch::evt_pouch_add_item, 0x0E0)
  // USER_FUNC(spm::evt_msg::evt_msg_print, 1, PTR(quickstartText), 0, 0)
  // USER_FUNC(spm::evt_msg::evt_msg_select, 1, PTR(quickstartOptions))
  // USER_FUNC(spm::evt_msg::evt_msg_continue)
  // SWITCH(LW(0))
  // END_SWITCH()
  USER_FUNC(spm::evt_seq::evt_seq_set_seq, spm::seqdrv::SEQ_MAPCHANGE, PTR("he1_01"), PTR("doa1_l"))
  RETURN()
  EVT_END()

  /*
      General mod functions
  */

  static void evt_patches()
  {
    spm::map_data::MapData * ls1_md = spm::map_data::mapDataPtr("ls1_01");
    spm::map_data::MapData * he1_md = spm::map_data::mapDataPtr("he1_01");
    spm::map_data::MapData * he2_md = spm::map_data::mapDataPtr("he2_07");
    spm::map_data::MapData * mi1_md = spm::map_data::mapDataPtr("mi1_07");
    spm::map_data::MapData * ta2_md = spm::map_data::mapDataPtr("ta2_04");
    spm::map_data::MapData * ta4_md = spm::map_data::mapDataPtr("ta4_12");
    spm::map_data::MapData * sp2_md = spm::map_data::mapDataPtr("sp2_01");
    spm::map_data::MapData * gn2_md = spm::map_data::mapDataPtr("gn2_02");
    spm::map_data::MapData * gn4_md = spm::map_data::mapDataPtr("gn4_03");

    evtpatch::hookEvtReplace(spm::aa1_01::aa1_01_mario_house_transition_evt, 10, determine_quickstart);
    evtpatch::hookEvtReplace(ls1_md->initScript, 1, ls1);
    evtpatch::hookEvtReplace(he1_md->initScript, 1, he1);
    evtpatch::hookEvtReplace(he2_md->initScript, 1, he2_mi1);
    evtpatch::hookEvtReplace(mi1_md->initScript, 1, he2_mi1);
    evtpatch::hookEvtReplace(ta2_md->initScript, 1, ta2);
    evtpatch::hookEvtReplace(ta4_md->initScript, 1, ta4);
    evtpatch::hookEvtReplace(sp2_md->initScript, 1, sp2);
    evtpatch::hookEvtReplace(gn2_md->initScript, 1, gn2);
    evtpatch::hookEvtReplace(gn4_md->initScript, 1, gn4);
}

void main()
{
    wii::os::OSReport("SPM Rel Loader: the mod has ran!\n");
    
    NetMemoryAccess::init();
    evtpatch::evtmgrExtensionInit();
    evt_patches();
    msgpatch::msgpatchMain();
    msgpatch::msgpatchAddEntry("msg_AP_item_name", "AP Item", false);
    msgpatch::msgpatchAddEntry("msg_AP_item_desc", "A valuable object from another dimension.", false);
    spm::item_data::itemDataTable[45].nameMsg = "msg_AP_item_name";
    spm::item_data::itemDataTable[45].descMsg = "msg_AP_item_desc";
    spm::item_data::itemDataTable[45].iconId = 324;
    pouchAddItem = patch::hookFunction(spm::mario_pouch::pouchAddItem, new_pouchAddItem);

    titleScreenCustomTextPatch();
}

}
