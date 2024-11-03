#include "mod.h"
#include "commandmanager.h"
#include "patch.h"
#include "netmemoryaccess.h"
#include "network.h"
#include "spmhttp.h"
#include "cutscene_helpers.h"
#include "evt_cmd.h"
#include "evtpatch.h"
#include "exception.h"
#include "evtdebug.h"

#include <spm/setup_data.h>
#include <spm/npcdrv.h>
#include <spm/seq_mapchange.h>
#include <spm/evt_seq.h>
#include <spm/effdrv.h>
#include <spm/evt_eff.h>
#include <spm/evt_item.h>
#include <spm/item_event_data.h>
#include <spm/evt_snd.h>
#include <spm/evt_msg.h>
#include <spm/mario_status.h>
#include <spm/evtmgr.h>
#include <spm/map_data.h>
#include <spm/fontmgr.h>
#include <spm/seqdrv.h>
#include <spm/seqdef.h>
#include <spm/seq_game.h>
#include <spm/spmario.h>
#include <spm/evt_mario.h>
#include <spm/mario.h>
#include <spm/mario_pouch.h>
#include <spm/system.h>
#include <wii/os/OSError.h>
#include <wii/os/OSThread.h>
#include <wii/os.h>
#include <msl/stdio.h>
#include <msl/string.h>

namespace mod {

  s32 startWebhook(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {
    registerPlayer();
    return 2;
  }

  s32 checkConnection(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {
    spm::evtmgr::EvtVar * args = (spm::evtmgr::EvtVar * ) evtEntry -> pCurData;
    if (isConnected) {
      spm::evtmgr_cmd::evtSetValue(evtEntry, args[0], 1);
    } else {
      spm::evtmgr_cmd::evtSetValue(evtEntry, args[0], 0);
    }
    return 2;
  }

  s32 startServerConnection(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {
    connectToServer();
    return 2;
  }

  s32 returnPos(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {
    spm::evtmgr::EvtVar * args = (spm::evtmgr::EvtVar * ) evtEntry -> pCurData;
    float positionX = getPositionXByClientID(1);
    float positionY = getPositionYByClientID(1);
    spm::evtmgr_cmd::evtSetFloat(evtEntry, args[0], positionX);
    spm::evtmgr_cmd::evtSetFloat(evtEntry, args[1], positionY);
    return 2;
  }

  s32 checkPosEqual(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {

    // LW 1 2 and 3 are the positions of the player on the server, LW 5 6 and 7 are the positions of the player on the client
    // Needs to be cast to int and back with some math in order to truncate the value properly so that extremely minor differences dont effect the outcome of the function
    f32 serverPosX = static_cast < int > (spm::evtmgr_cmd::evtGetFloat(evtEntry, evtEntry -> lw[1]) * 100) / 100.0f;
    f32 serverPosY = static_cast < int > (spm::evtmgr_cmd::evtGetFloat(evtEntry, evtEntry -> lw[2]) * 100) / 100.0f;
    f32 serverPosZ = static_cast < int > (spm::evtmgr_cmd::evtGetFloat(evtEntry, evtEntry -> lw[3]) * 100) / 100.0f;
    f32 clientPosX = static_cast < int > (spm::evtmgr_cmd::evtGetFloat(evtEntry, evtEntry -> lw[5]) * 100) / 100.0f;
    f32 clientPosY = static_cast < int > (spm::evtmgr_cmd::evtGetFloat(evtEntry, evtEntry -> lw[6]) * 100) / 100.0f;
    f32 clientPosZ = static_cast < int > (spm::evtmgr_cmd::evtGetFloat(evtEntry, evtEntry -> lw[7]) * 100) / 100.0f;

    wii::os::OSReport("PosX %f %f\n", serverPosX, clientPosX);
    wii::os::OSReport("PosY %f %f\n", serverPosY, clientPosY);
    wii::os::OSReport("PosZ %f %f\n", serverPosZ, clientPosZ);

    const f32 tolerance = 4.0;

    spm::npcdrv::NPCEntry * ownerNpc = (spm::npcdrv::NPCEntry *)evtEntry -> ownerNPC;
    if (abs(serverPosY - clientPosY) <= 0.2)
    {
      ownerNpc -> unitWork[15] = 0; //Run
    } else {
      ownerNpc -> unitWork[15] = 1; //Jump
    }


    // Check if the absolute differences are within the tolerance
    if (abs(serverPosX - clientPosX) <= tolerance &&
      abs(serverPosY - clientPosY) <= tolerance &&
      abs(serverPosZ - clientPosZ) <= tolerance) {
      evtEntry -> lw[8] = 1;
    } else {
      evtEntry -> lw[8] = 0;
    }

    return 2;
  }

  s32 checkPlayerDC(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {
    spm::npcdrv::NPCEntry * ownerNpc = (spm::npcdrv::NPCEntry *)evtEntry -> ownerNPC;
    s32 client = ownerNpc -> unitWork[0];

    if (getDCByClientID(client) == true) {
      evtEntry -> lw[10] = 1;
      removePlayer(client);
    }
    return 2;
  }

  s32 npcDeletePlayer(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {
    spm::npcdrv::NPCEntry * ownerNpc = (spm::npcdrv::NPCEntry *)evtEntry -> ownerNPC;
    removePlayer(ownerNpc -> unitWork[0]);
    return 2;
  }

  s32 adjustJumpSpeed(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {
      const f32 maxHeight = 128.0;
      const f32 maxSpeed = 400.0;
      f32 height = evtEntry -> lw[8];

      if (height >= maxHeight) {
          evtEntry -> lw[8] = maxSpeed;
      }

      // Otherwise, calculate speed proportionally to height
      f32 speed = (height / maxHeight) * maxSpeed;
      if (speed < 200.0){
        speed = 200.0;
      }
      evtEntry -> lw[8] = speed;
      return 2;
}

  s32 npcGrabName(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {
    spm::npcdrv::NPCEntry * ownerNpc = (spm::npcdrv::NPCEntry *)evtEntry -> ownerNPC;

    spm::evtmgr_cmd::evtSetValue(evtEntry, evtEntry -> lw[10], (int)ownerNpc -> name);
    return 2;
  }

s32 modWaitAnimEnd(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {

  if (spm::evt_npc::evt_npc_wait_anim_end(evtEntry, firstRun) == 2) return 2;
  checkPosEqual(evtEntry, firstRun);
  if (evtEntry -> lw[8] == 0) {
    return 2;
  }
  evtEntry -> lw[8] = 0;
  return 0;
}

s32 summonThunder(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {
  spm::evtmgr::EvtVar * args = (spm::evtmgr::EvtVar * ) evtEntry -> pCurData;
  f32 npcX = spm::evtmgr_cmd::evtGetFloat(evtEntry, args[0]);
  f32 npcY = spm::evtmgr_cmd::evtGetFloat(evtEntry, args[1]);
  f32 npcZ = spm::evtmgr_cmd::evtGetFloat(evtEntry, args[2]);
  //spm::effdrv::EffEntry* tcEntry = spm::effdrv::eff_item_thunder(0, 0, 0, 0, 0, 1, 0, 0);
  //func_800a315c(tcEntry, -1, "TC");
  return 2;
}

s32 activateTC(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {
  spm::evtmgr::evtEntry(thunderCloud, 0, 0);
  return 2;
}

s32 addCloudToList(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {
  char * tcName  = "TC";
  spm::npcdrv::NPCEntry * tc = spm::npcdrv::npcNameToPtr(tcName);
  spm::item_event_data::item_event_data_wp -> wp -> ItemNpcRef.npcId = tc -> id;
  return 2;
}

s32 setScale(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {
  spm::evtmgr::EvtVar * args = (spm::evtmgr::EvtVar * ) evtEntry -> pCurData;
  f32 scale = spm::evtmgr_cmd::evtGetFloat(evtEntry, args[0]);

  spm::mario::MarioWork* wp = spm::mario::marioGetPtr();
  wp -> scale.x = scale;
  wp -> scale.y = scale;
  wp -> scale.z = scale;
  return 2;
}

s32 setGravity(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {
  spm::evtmgr::EvtVar * args = (spm::evtmgr::EvtVar * ) evtEntry -> pCurData;
  spm::npcdrv::NPCEntry * ownerNpc = (spm::npcdrv::NPCEntry *)evtEntry -> ownerNPC;
  f32 gravity = spm::evtmgr_cmd::evtGetFloat(evtEntry, args[0]);

  ownerNpc -> gravity = gravity;
  return 2;
}

s32 evt_marioStatusApplyStatuses(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {
  spm::evtmgr::EvtVar * args = (spm::evtmgr::EvtVar * ) evtEntry -> pCurData;
  s32 status = args[0];

  spm::mario_status::marioStatusApplyStatuses(status, 2);
  return 2;
}

s32 npcGetPlayerMot(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {
  spm::evtmgr::EvtVar * args = (spm::evtmgr::EvtVar * ) evtEntry -> pCurData;
  spm::npcdrv::NPCEntry * ownerNpc = (spm::npcdrv::NPCEntry *)evtEntry -> ownerNPC;

  spm::evtmgr_cmd::evtSetValue(evtEntry, args[0], getMotionIdByClientID(ownerNpc -> unitWork[0]));
  return 2;
}

s32 npcSetPlayerMot(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {
  spm::evtmgr::EvtVar * args = (spm::evtmgr::EvtVar * ) evtEntry -> pCurData;
  spm::npcdrv::NPCEntry * ownerNpc = (spm::npcdrv::NPCEntry *)evtEntry -> ownerNPC;

  u16 motionId = args[0];
  setMotionIdByClientID(ownerNpc -> unitWork[0], motionId);
  return 2;
}

spm::npcdrv::NPCTribeAnimDef animsThunderCloud[] = {
    {0, "S_1"}, // Standing (idle)
    {1, "W_1"}, // Walking
    {2, "R_1"}, // Running
    {3, "A_1A"}, // Grinning?
    {4, "A_2A"}, // Start charging thunder
    {5, "A_2B"}, // Thunder charge loop
    {6, "A_3A"}, // Release thunder
    {-1, nullptr}
};

EVT_DECLARE_USER_FUNC(startWebhook, 0)
EVT_DECLARE_USER_FUNC(startServerConnection, 0)
EVT_DECLARE_USER_FUNC(checkConnection, 1)
EVT_DECLARE_USER_FUNC(returnPos, 2)
EVT_DECLARE_USER_FUNC(updateServerPos, 0)
EVT_DECLARE_USER_FUNC(npcGetPlayerPos, 0)
EVT_DECLARE_USER_FUNC(npcGetPlayerMot, 1)
EVT_DECLARE_USER_FUNC(npcSetPlayerMot, 1)
EVT_DECLARE_USER_FUNC(npcFixAnims, 0)
EVT_DECLARE_USER_FUNC(npcGrabName, 0)
EVT_DECLARE_USER_FUNC(checkPosEqual, 0)
EVT_DECLARE_USER_FUNC(npcDeletePlayer, 0)
EVT_DECLARE_USER_FUNC(adjustJumpSpeed, 0)
EVT_DECLARE_USER_FUNC(checkPlayerDC, 0)
EVT_DECLARE_USER_FUNC(modWaitAnimEnd, 2)
EVT_DECLARE_USER_FUNC(summonThunder, 3)
EVT_DECLARE_USER_FUNC(activateTC, 0)
EVT_DECLARE_USER_FUNC(addCloudToList, 0)
EVT_DECLARE_USER_FUNC(setScale, 1)
EVT_DECLARE_USER_FUNC(setGravity, 1)
EVT_DECLARE_USER_FUNC(evt_marioStatusApplyStatuses, 1)

EVT_BEGIN(insertNop)
  SET(LW(0), LW(0))
RETURN_FROM_CALL()

EVT_BEGIN(fixName)
  SET(LW(2), 1)
  USER_FUNC(addCloudToList)
RETURN_FROM_CALL()

EVT_BEGIN(mariounk3)
  SET(LW(1), 50)
RETURN_FROM_CALL()

EVT_BEGIN(mariounk6)
  USER_FUNC(npcDeletePlayer)
RETURN_FROM_CALL()

EVT_BEGIN(playerMainLogic)
  USER_FUNC(spm::evt_npc::evt_npc_set_move_mode, PTR("me"), 1)
  USER_FUNC(setGravity, FLOAT(0.0))
  LBL(0)
  USER_FUNC(checkPlayerDC)
  IF_EQUAL(LW(10), 1)
    USER_FUNC(spm::evt_npc::evt_npc_get_position, PTR("me"), LW(0), LW(1), LW(2))
    USER_FUNC(spm::evt_eff::evt_eff, 0, PTR("dmen_warp"), 0, LW(0), LW(1), LW(2), 0, 0, 0, 0, 0, 0, 0, 0)
    USER_FUNC(spm::evt_snd::evt_snd_sfxon_3d, PTR("SFX_BS_DMN_GOOUT1"), LW(0), LW(1), LW(2))
    USER_FUNC(spm::evt_npc::evt_npc_delete, PTR("me"))
  END_IF()
  USER_FUNC(npcGetPlayerPos)
  USER_FUNC(spm::evt_npc::evt_npc_get_position, PTR("me"), LW(5), LW(6), LW(7))
  USER_FUNC(checkPosEqual)
  IF_EQUAL(LW(8), 0)
    USER_FUNC(spm::evt_npc::evt_npc_get_unitwork, PTR("me"), 15, LW(8))
    IF_EQUAL(LW(8), 1)
      USER_FUNC(npcGetPlayerMot, LW(9))
      IF_NOT_EQUAL(LW(9), 6000)
      USER_FUNC(npcFixAnims)
      USER_FUNC(spm::evt_npc::evt_npc_set_anim, PTR("me"), 0x19, 0)
      ELSE()
        USER_FUNC(npcFixAnims)
        USER_FUNC(spm::evt_npc::evt_npc_set_anim, PTR("me"), 6, 0)
      END_IF()
      SET(LW(8), LW(2))
      SUB(LW(8), LW(6))
      USER_FUNC(adjustJumpSpeed)
      USER_FUNC(spm::evt_npc::evt_npc_jump_to, PTR("me"), LW(1), LW(2), LW(3), FLOAT(0.0), LW(8))
      //USER_FUNC(spm::evt_snd::evt_snd_sfxon_npc, PTR("SFX_P_MARIO_LAND1"), PTR("me"))
    ELSE()
      USER_FUNC(npcGetPlayerMot, LW(9))
      IF_NOT_EQUAL(LW(9), 0x03)
      IF_NOT_EQUAL(LW(9), 6000)
        USER_FUNC(npcFixAnims)
        USER_FUNC(spm::evt_npc::evt_npc_set_anim, PTR("me"), 2, 0)
      ELSE()
        USER_FUNC(npcFixAnims)
        USER_FUNC(spm::evt_npc::evt_npc_set_anim, PTR("me"), 6, 0)
      END_IF()
      END_IF()
      USER_FUNC(spm::evt_npc::evt_npc_walk_to, PTR("me"), LW(1), LW(3), 0, FLOAT(180.0), 4, 0, 0)
    END_IF()
  ELSE()
    USER_FUNC(spm::evt_npc::evt_npc_get_cur_anim, PTR("me"), LW(8))
    USER_FUNC(npcGetPlayerMot, LW(9))
    IF_EQUAL(LW(9), 0x00)
      SWITCH(LW(8))
        CASE_EQUAL(0)
          USER_FUNC(npcFixAnims)
          USER_FUNC(spm::evt_npc::evt_npc_set_anim, PTR("me"), 0, 0)
        CASE_EQUAL(2)
          USER_FUNC(npcFixAnims)
          USER_FUNC(spm::evt_npc::evt_npc_set_anim, PTR("me"), 0, 0)
        CASE_ETC()
          USER_FUNC(spm::evt_npc::evt_npc_wait_anim_end, PTR("me"), 1)
          USER_FUNC(npcFixAnims)
          USER_FUNC(spm::evt_npc::evt_npc_set_anim, PTR("me"), 0, 0)
      END_SWITCH()
    ELSE()
      IF_EQUAL(LW(9), 5000)
        USER_FUNC(npcFixAnims)
        USER_FUNC(spm::evt_npc::evt_npc_set_anim, PTR("me"), 22, 0)
      END_IF()
      IF_EQUAL(LW(9), 6000)
        USER_FUNC(npcFixAnims)
        USER_FUNC(spm::evt_npc::evt_npc_set_anim, PTR("me"), 6, 0)
      END_IF()
    END_IF()
  END_IF()
  WAIT_FRM(1)
  GOTO(0)
  RETURN()
EVT_END()

EVT_BEGIN(registerToServer)
  USER_FUNC(spm::evt_msg::evt_msg_print, 1, PTR(startText1), 0, 0)
  USER_FUNC(startWebhook)
  USER_FUNC(spm::evt_msg::evt_msg_print, 1, PTR(startText2), 0, 0)
  USER_FUNC(spm::evt_seq::evt_seq_set_seq, spm::seqdrv::SEQ_MAPCHANGE, PTR("mac_02"), PTR("elv1"))
RETURN_FROM_CALL()

EVT_BEGIN(evt_connectToServer)
  WAIT_MSEC(1000)
  USER_FUNC(checkConnection, LW(0))
  IF_EQUAL(LW(0), 0)
    USER_FUNC(spm::evt_msg::evt_msg_print, 1, PTR(connectionText1), 0, 0)
    USER_FUNC(startServerConnection)
    USER_FUNC(spm::evt_msg::evt_msg_print, 1, PTR(connectionText2), 0, 0)
    WAIT_MSEC(1000)
  END_IF()
RETURN_FROM_CALL()

EVT_BEGIN(evt_spawn_players)
    WAIT_FRM(1)
    USER_FUNC(spm::evt_npc::evt_npc_entry_from_template, 0, 422, 0, -100, 0, LW(10), EVT_NULLPTR)
    USER_FUNC(spm::evt_npc::evt_npc_flip_to, LW(10), 1)
    USER_FUNC(spm::evt_npc::evt_npc_finish_flip_instant, LW(10))
    USER_FUNC(spm::evt_snd::evt_snd_sfxon_npc, PTR("SFX_EVT_100_PC_LINE_DRAW1"), LW(10))
    USER_FUNC(spm::evt_snd::evt_snd_sfxon_npc, PTR("SFX_EVT_100_PC_LINE_TURN1"), LW(10))
    USER_FUNC(spm::evt_npc::evt_npc_flip, LW(10))
    USER_FUNC(spm::evt_npc::evt_npc_set_part_attack_power, LW(10), 1, LW(5))
    MUL(LW(5), 2)
    USER_FUNC(spm::evt_npc::evt_npc_set_part_attack_power, LW(10), 2, LW(5))
    USER_FUNC(spm::evt_npc::evt_npc_set_hp, LW(10), LW(6))
    USER_FUNC(spm::evt_npc::evt_npc_set_position, LW(10), LW(1), LW(2), LW(3))
    USER_FUNC(spm::evt_npc::evt_npc_set_unitwork, LW(10), 0, LW(7))
    ADD(LW(4), 1)
    IF_EQUAL(LW(4), LW(0))
      DO_BREAK()
    END_IF()
RETURN()
EVT_END()

EVT_BEGIN(spawnThunderCloud)
    //USER_FUNC(spm::evt_mario::evt_mario_key_off, 0)
    SPAWN_CHARACTER("TC", "e_kmoon", animsThunderCloud)
    USER_FUNC(spm::evt_mario::evt_mario_get_pos, LW(1), LW(2), LW(3))
    ADD(LW(2), 75)
    USER_FUNC(spm::evt_npc::evt_npc_set_position, PTR("TC"), LW(1), LW(2), LW(3))
    USER_FUNC(spm::evt_npc::evt_npc_set_anim, PTR("TC"), 1, true)
    USER_FUNC(activateTC)
RETURN()
EVT_END()

EVT_BEGIN(setResults)
  SET(LW(10), 2)
RETURN_FROM_CALL()

EVT_BEGIN(thunderCloud)
    USER_FUNC(evt_marioStatusApplyStatuses, 0x80)
    DO(150)
      USER_FUNC(spm::evt_mario::evt_mario_get_pos, LW(1), LW(2), LW(3))
      ADD(LW(2), 75)
      USER_FUNC(spm::evt_npc::evt_npc_set_position, PTR("TC"), LW(1), LW(2), LW(3))
      WAIT_FRM(1)
    WHILE()
    USER_FUNC(spm::evt_npc::evt_npc_set_anim, PTR("TC"), 5, true)
    USER_FUNC(spm::evt_snd::evt_snd_sfxon_npc, PTR("SFX_I_BIRIBIRI1"), PTR("TC"))
    DO(30)
      USER_FUNC(spm::evt_mario::evt_mario_get_pos, LW(1), LW(2), LW(3))
      ADD(LW(2), 75)
      USER_FUNC(spm::evt_npc::evt_npc_set_position, PTR("TC"), LW(1), LW(2), LW(3))
      WAIT_FRM(1)
    WHILE()
    USER_FUNC(spm::evt_npc::evt_npc_set_anim, PTR("TC"), 1, true)
    DO(150)
      USER_FUNC(spm::evt_mario::evt_mario_get_pos, LW(1), LW(2), LW(3))
      ADD(LW(2), 75)
      USER_FUNC(spm::evt_npc::evt_npc_set_position, PTR("TC"), LW(1), LW(2), LW(3))
      WAIT_FRM(1)
    WHILE()
    USER_FUNC(spm::evt_npc::evt_npc_set_anim, PTR("TC"), 5, true)
    USER_FUNC(spm::evt_snd::evt_snd_sfxon_npc, PTR("SFX_I_BIRIBIRI1"), PTR("TC"))
    DO(30)
      USER_FUNC(spm::evt_mario::evt_mario_get_pos, LW(1), LW(2), LW(3))
      ADD(LW(2), 75)
      USER_FUNC(spm::evt_npc::evt_npc_set_position, PTR("TC"), LW(1), LW(2), LW(3))
      WAIT_FRM(1)
    WHILE()
    USER_FUNC(spm::evt_npc::evt_npc_set_anim, PTR("TC"), 2, true)
    DO(180)
      USER_FUNC(spm::evt_mario::evt_mario_get_pos, LW(1), LW(2), LW(3))
      ADD(LW(2), 75)
      USER_FUNC(spm::evt_npc::evt_npc_set_position, PTR("TC"), LW(1), LW(2), LW(3))
      WAIT_FRM(1)
    WHILE()
    USER_FUNC(spm::evt_npc::evt_npc_set_anim, PTR("TC"), 5, true)
    USER_FUNC(spm::evt_snd::evt_snd_sfxon_npc, PTR("SFX_I_BIRIBIRI1"), PTR("TC"))
    DO(30)
      USER_FUNC(spm::evt_mario::evt_mario_get_pos, LW(1), LW(2), LW(3))
      ADD(LW(2), 75)
      USER_FUNC(spm::evt_npc::evt_npc_set_position, PTR("TC"), LW(1), LW(2), LW(3))
      WAIT_FRM(1)
    WHILE()
    USER_FUNC(spm::evt_mario::evt_mario_key_off, 0)
    USER_FUNC(spm::evt_mario::evt_mario_get_pos, LW(1), LW(2), LW(3))
    USER_FUNC(spm::evt_snd::evt_snd_sfxon_npc, PTR("SFX_I_THUNDER1"), PTR("TC"))
    USER_FUNC(spm::evt_eff::evt_eff, 0, PTR("jigen_paralysis"), 0, LW(1), LW(2), LW(3), 0, 0, 0, 0, 0, 0, 0, 0)
    USER_FUNC(setScale, FLOAT(0.5))
    WAIT_MSEC(1000)
    USER_FUNC(spm::evt_mario::evt_mario_key_on)
    USER_FUNC(spm::evt_npc::evt_npc_get_position, PTR("TC"), LW(0), LW(1), LW(2))
    USER_FUNC(spm::evt_eff::evt_eff, 0, PTR("dmen_warp"), 0, LW(0), LW(1), LW(2), 0, 0, 0, 0, 0, 0, 0, 0)
    USER_FUNC(spm::evt_snd::evt_snd_sfxon_3d, PTR("SFX_BS_DMN_GOOUT1"), LW(0), LW(1), LW(2))
    USER_FUNC(spm::evt_npc::evt_npc_delete, PTR("TC"))
    WAIT_MSEC(15000)
    USER_FUNC(setScale, FLOAT(1.0))
RETURN()
EVT_END()

}
