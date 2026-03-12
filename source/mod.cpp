#include "mod.h"
#include "commandmanager.h"
#include "commands.h"
#include "evtpatch.h"
#include "patch.h"
#include "msgpatch.h"
#include "netmemoryaccess.h"
#include "network.h"

#include <spm/rel/aa1_01.h>
#include <spm/fontmgr.h>
#include <spm/seqdrv.h>
#include <spm/evt_item.h>
#include <spm/itemdrv.h>
#include <spm/evtmgr.h>
#include <spm/effdrv.h>
#include <spm/eff_sub.h>
#include <spm/memory.h>
#include <spm/seqdef.h>
#include <spm/item_data.h>
#include <spm/mario_pouch.h>
#include <spm/evt_pouch.h>
#include <spm/evt_seq.h>
#include <spm/filemgr.h>
#include <spm/map_data.h>
#include <spm/spmario.h>
#include <wii/os/OSError.h>
#include <msl/stdio.h>

namespace mod {

ItemCheckList itemCheckList[MAX_CHECK_LIST] = 
{
  {
    611, 0x0DF
  },
  {
    612, 0x0DE
  }
};

/*
    Title Screen Custom Text
    Prints "SPM Rel Loader" at the top of the title screen
*/

static spm::seqdef::SeqFunc *seq_titleMainReal;
static void seq_titleMainOverride(spm::seqdrv::SeqWork *wp)
{
    wii::gx::GXColor _colour {0, 255, 0, 255};
    f32 scale = 0.8f;
    static char msg[128];
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

  bool ( * pouchAddItem)(s32 itemId);
  spm::itemdrv::ItemEntry * ( * itemEntry)(const char * name, s32 type, s32 behaviour, f32 x, f32 y, f32 z, spm::evtmgr::EvtScriptCode * pickupScript, spm::evtmgr::EvtVar switchNumber);

  bool new_pouchAddItem(s32 itemId)
  {
    if (itemId == 45)
    {
      return true;
    }
    return pouchAddItem(itemId);
  }

  bool itemAdded = false; 
  bool ranOnce = false;

  bool new_itemCollectPouchItem(spm::itemdrv::ItemEntry *item)
  {
    //wii::os::OSReport("GSWF: %d\n", item->switchNumber);
      if (item->switchNumber != 0x0)
        {
          bool ret = spm::itemdrv::itemCollectPouchItem(item);
          if (!itemAdded)
          {
            addToGswfStack(item);
            itemAdded = true;
          }
          if (ret)
          {
            itemAdded = false;
          }
          return ret;
        }
        else {
          if (!ranOnce)
          {
            ranOnce = true;
            wii::os::OSReport("Added item with switch %d to stack\n", item->switchNumber);
            static char msg[128];
            msl::stdio::snprintf(msg, 128, "Collected item ID %d\n", item->type);

            NetMemoryAccess::enqueuePacket(0x1001, msg, msl::string::strlen(msg) + 1);

            wii::os::OSReport("Queued one test packet.\n");
          }
        }

        return spm::itemdrv::itemCollectPouchItem(item);
    }

spm::evtmgr::EvtVar convertGswfToIndex(spm::evtmgr::EvtVar switchNumber)
{
  spm::evtmgr::EvtVar gswf = abs(switchNumber);
  gswf -= (abs(GSWF(0)));
  gswf = abs(gswf);
  return gswf;
}

  // does basically nothing for now but will be useful later for swapping pixls/characters around
  spm::itemdrv::ItemEntry *new_itemEntry(const char * name, s32 type, s32 behaviour, f32 x, f32 y, f32 z, spm::evtmgr::EvtScriptCode * pickupScript, spm::evtmgr::EvtVar switchNumber)
  {
    if (switchNumber != 0x0)
    {
      for (u32 i = 0; i < MAX_CHECK_LIST; i++)
      {
        if (itemCheckList[i].gswfIndex == convertGswfToIndex(switchNumber))
        {
          type = itemCheckList[i].itemid;
          break;
        }
      }
      
      spm::itemdrv::ItemEntry * item = itemEntry(name, type, behaviour, x, y, z, pickupScript, switchNumber);
      if (!item)
      {
        return item;
      }
      return item;
    }
    return itemEntry(name, type, behaviour, x, y, z, pickupScript, switchNumber);
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

  EVT_BEGIN(mac_02)
    SET(GSW(0), 359)
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
    spm::map_data::MapData * mac_02_md = spm::map_data::mapDataPtr("mac_02");

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
    evtpatch::hookEvtReplace(mac_02_md->initScript, 1, mac_02);
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
      itemEntry = patch::hookFunction(spm::itemdrv::itemEntry, new_itemEntry);
      writeBranchLink(spm::itemdrv::itemMain, 0xA18, new_itemCollectPouchItem);
      titleScreenCustomTextPatch();
  }
}