#include "mod.h"
#include "commandmanager.h"
#include "patch.h"
#include "netmemoryaccess.h"
#include "network.h"
#include "spmhttp.h"
#include "core_json.h"
#include "chainloader.h"
#include "cutscene_helpers.h"
#include "evt_cmd.h"
#include "evtpatch.h"
#include "exception.h"
#include "evtdebug.h"
#include "romfontexpand.h"

#include <spm/setup_data.h>
#include <spm/npcdrv.h>
#include <spm/seq_mapchange.h>
#include <spm/evt_seq.h>
#include <spm/evt_msg.h>
#include <spm/evtmgr.h>
#include <spm/map_data.h>
#include <spm/fontmgr.h>
#include <spm/seqdrv.h>
#include <spm/seqdef.h>
#include <spm/spmario.h>
#include <spm/mario.h>
#include <spm/mario_pouch.h>
#include <spm/system.h>
#include <wii/os/OSError.h>
#include <wii/ipc.h>
#include <wii/os.h>
#include <msl/stdio.h>
#include <msl/string.h>
#include <EASTL/string.h>

namespace mod {
  bool gIsDolphin;
  bool tfirstRun = false;
  bool isConnected = false;

  struct Player {
    int clientID;
    float positionX;
    float positionY;
    float positionZ;
    int attack;
    int maxHP;
    int currentHP;
    bool isDead;

    void heal(int health) {
      currentHP += health;
      if (currentHP > maxHP) currentHP = maxHP;
    }
    void damage(int health) {
      currentHP -= health;
      if (currentHP > maxHP) currentHP = maxHP;
      if (currentHP < 0) {
        currentHP = 0;
        isDead = true;
      };
    }
  };

  Player clients[24];
  int numClients = 0;

  /*
      Title Screen Custom Text
      Prints "SPM Rel Loader" at the top of the title screen
  */

  static spm::seqdef::SeqFunc * seq_titleMainReal;
  static void seq_titleMainOverride(spm::seqdrv::SeqWork * wp) {
    wii::gx::GXColor _colour {
      0,
      255,
      0,
      255
    };
    f32 scale = 0.8;
    char msg[128];
    u32 ip = Mynet_gethostip();
    msl::stdio::snprintf(msg, 128, "%d.%d.%d.%d\n", ip >> 24 & 0xff, ip >> 16 & 0xff, ip >> 8 & 0xff, ip & 0xff);
    spm::fontmgr::FontDrawStart();
    spm::fontmgr::FontDrawEdge();
    spm::fontmgr::FontDrawColor( & _colour);
    spm::fontmgr::FontDrawScale(scale);
    spm::fontmgr::FontDrawNoiseOff();
    spm::fontmgr::FontDrawRainbowColorOff();
    f32 x = -((spm::fontmgr::FontGetMessageWidth(msg) * scale) / 2);
    spm::fontmgr::FontDrawString(x, 200.0, msg);
    seq_titleMainReal(wp);
    isConnected = false;
  }
  static void titleScreenCustomTextPatch() {
    seq_titleMainReal = spm::seqdef::seq_data[spm::seqdrv::SEQ_TITLE].main;
    spm::seqdef::seq_data[spm::seqdrv::SEQ_TITLE].main = & seq_titleMainOverride;
  }

  static void checkForDolphin() {
    // Thanks to TheLordScruffy for telling me about this
    gIsDolphin = wii::ipc::IOS_Open("/sys", 1) == -106;

    // If they ever fix that, it'll be in a version that's definitely new enough to have /dev/dolphin
    if (!gIsDolphin) {
      int ret = wii::ipc::IOS_Open("/dev/dolphin", 0);
      if (ret >= 0) {
        gIsDolphin = true;
        wii::ipc::IOS_Close(ret);
      }
    }
  }

  /*
      Networking functions
  */
  s32 roundi(f32 x) {
    if (!(x >= 0.0f))
        return -(s32) (0.5 - x);
    else
        return (s32) (0.5 + x);
}

  const char * strrchr(const char * str, char c) {
    const char * lastOccurrence = nullptr;
    const char * currentPos = str;

    // Loop through the string and find the last occurrence of 'c'
    while ((currentPos = strchr(currentPos, c)) != nullptr) {
      lastOccurrence = currentPos; // Store the last found position
      currentPos++; // Move to the next character to continue searching
    }

    return lastOccurrence; // Return the last occurrence (or NULL if not found)
  }

  int atoi(const char * str) {
    int result = 0;
    int sign = 1;
    int i = 0;

    // Handle negative numbers
    if (str[0] == '-') {
      sign = -1;
      i++; // Skip the negative sign
    }

    // Convert each digit to an integer
    for (; str[i] != '\0'; ++i) {
      if (str[i] >= '0' && str[i] <= '9') {
        result = result * 10 + (str[i] - '0');
      } else {
        // If a non-digit character is found, stop processing
        break;
      }
    }

    return result * sign;
  }


  const int MAX_TOKENS = 100;
  s32 joiningClients[MAX_TOKENS];

  HTTPStatus_t webhookShenanigans() {
    wii::os::OSReport("webhookShenanigans has ran!\n");
    HTTPResponse_t response = {
      0
    };
    u8 responseBuffer = new u8[1024];
    response.pBuffer = responseBuffer;
    response.bufferLen = 1024;

    HTTPStatus_t mystatus = HTTPGet("76.138.196.253", 3000, "/", & response);
    wii::os::OSReport("Status: %d\n", mystatus);

    //get rid of the garbage data
    char dest[response.bufferLen + 1]; // +1 for the null terminator
    strncpy(dest, (const char * ) response.pBuffer, response.bufferLen);

    const char * lastNewline = strrchr(dest, '\n');
    // Move the pointer to the first character after the last newline.
    const char * lastLine = lastNewline + 1;
    char dest1[response.bufferLen + 1];
    strcpy(dest1, lastLine); // Copies the last line into the buffer.

    wii::os::OSReport("Status: %s\n", dest1);
    return mystatus;
  }

  HTTPStatus_t registerPlayer() {
    wii::os::OSReport("registerPlayer has ran!\n");
    HTTPResponse_t response = {
      0
    };
    u8 responseBuffer = new u8[1024];
    response.pBuffer = responseBuffer;
    response.bufferLen = 1024;

    //u8 postBuffer = new u8[1024];

    const char postBuffer[1024];
    spm::spmario::gp -> gsw[2000] = spm::system::irand(255);
    spm::spmario::gp -> gsw[2001] = spm::system::irand(255);
    spm::mario_pouch::MarioPouchWork * pouch_ptr = spm::mario_pouch::pouchGetPtr();
    snprintf(postBuffer, sizeof(postBuffer), "%d.%d.%s.%d.%d.%d.%d.%d",
      spm::spmario::gp -> gsw[2000],
      spm::spmario::gp -> gsw[2001],
      spm::spmario::gp -> saveName,
      pouch_ptr -> level,
      pouch_ptr -> xp,
      pouch_ptr -> hp,
      pouch_ptr -> maxHp,
      pouch_ptr -> attack
    );
    wii::os::OSReport("Level: %d\n", pouch_ptr -> level);

    HTTPStatus_t mystatus = HTTPSendRequest("76.138.196.253", 3000, "/register", HTTP_METHOD_POST, postBuffer, strlen(postBuffer), & response);
    wii::os::OSReport("Status: %d\n", mystatus);
    //wii::os::OSReport("Status: %s\n", response.pBuffer);
    //get rid of the garbage data
    char dest[response.bufferLen + 1]; // +1 for the null terminator
    strncpy(dest, (const char * ) response.pBuffer, response.bufferLen);

    const char * lastNewline = strrchr(dest, '\n');
    // Move the pointer to the first character after the last newline.
    const char * lastLine = lastNewline + 1;
    char dest1[response.bufferLen + 1];
    strcpy(dest1, lastLine); // Copies the last line into the buffer.
    spm::spmario::gp -> gsw[2002] = atoi(dest1);
    wii::os::OSReport("ClientID: %d\n", spm::spmario::gp -> gsw[2002]);
    HTTPFree( & response);
    return mystatus;
  }

  HTTPStatus_t connectToServer() {
    wii::os::OSReport("connectToServer has ran!\n");
    HTTPResponse_t response = {
      0
    };
    u8 responseBuffer = new u8[1024];
    response.pBuffer = responseBuffer;
    response.bufferLen = 1024;

    //u8 postBuffer = new u8[1024];

    const char postBuffer[1024];
    spm::mario_pouch::MarioPouchWork * pouch_ptr = spm::mario_pouch::pouchGetPtr();
    spm::mario::MarioWork * mwpp = spm::mario::marioGetPtr();
    wii::mtx::Vec3 pos = mwpp -> position;
    snprintf(postBuffer, sizeof(postBuffer), "%d.%d.%d.%s.%d.%d.%d.%d.%d.%s.%d.%d.%d",
      spm::spmario::gp -> gsw[2000],
      spm::spmario::gp -> gsw[2001],
      spm::spmario::gp -> gsw[2002],
      spm::spmario::gp -> saveName,
      pouch_ptr -> level,
      pouch_ptr -> xp,
      pouch_ptr -> hp,
      pouch_ptr -> maxHp,
      pouch_ptr -> attack,
      spm::spmario::gp -> mapName,
      roundi(pos.x * 1000),
      roundi(pos.y * 1000),
      roundi(pos.z * 1000)
    );

    wii::os::OSReport("Position: %f %f %f\n", pos.x, pos.y, pos.z);

    HTTPStatus_t mystatus = HTTPSendRequest("76.138.196.253", 3000, "/connect", HTTP_METHOD_POST, postBuffer, strlen(postBuffer), & response);
    wii::os::OSReport("Status: %d\n", mystatus);
    //wii::os::OSReport("Status: %s\n", response.pBuffer);
    //get rid of the garbage data
    char dest[response.bufferLen + 1]; // +1 for the null terminator
    strncpy(dest, (const char * ) response.pBuffer, response.bufferLen);

    const char * lastNewline = strrchr(dest, '\n');
    // Move the pointer to the first character after the last newline.
    const char * lastLine = lastNewline + 1;
    char dest1[response.bufferLen + 1];
    strcpy(dest1, lastLine); // Copies the last line into the buffer.
    isConnected = true;
    wii::os::OSReport("ClientID: %d\n", spm::spmario::gp -> gsw[2002]);
    HTTPFree( & response);
    return mystatus;
  }

  s32 checkForPlayersJoiningRoom(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {
      u8 responseBuffer[1024];
      const char postBuffer[1024];

      snprintf(postBuffer, sizeof(postBuffer), "checkForPlayers.%d.%d.%d.%s.%s",
          spm::spmario::gp->gsw[2002],
          spm::spmario::gp->gsw[2000],
          spm::spmario::gp->gsw[2001],
          spm::spmario::gp->saveName,
          spm::spmario::gp->mapName
      );

      s32 responseBytes = SendUDP("76.138.196.253", 4000, postBuffer, strlen(postBuffer), responseBuffer, 1024);

      // Ensure data was received
      if (responseBytes > 0) {
          wii::os::OSReport("Response Bytes: %d\n", responseBytes);

          // Reinterpret the response buffer as an array of integers
          u8* clientIDs = reinterpret_cast<u8*>(responseBuffer);
          int numClients = responseBytes / sizeof(u8);  // Calculate how many integers were received

          wii::os::OSReport("Number of Clients: %d\n", numClients);  // Log the number of integers (clients)

          if (numClients > 0) {
              for (int i = 0; i < numClients; ++i) {
                  joiningClients[i] = static_cast<s32>(clientIDs[i]);  // Assign the converted client ID
                  wii::os::OSReport("ClientID: %d\n", static_cast<s32>(clientIDs[i]));  // Report the client ID
              }

              evtEntry->lw[0] = numClients;
              wii::os::OSReport("Total Clients Processed: %d\n", evtEntry->lw[0]);
          } else {
              evtEntry->lw[0] = 0;
              wii::os::OSReport("No valid clients found.\n");
          }
      } else {
          wii::os::OSReport("No response received or an error occurred\n");
      }


      return 2;
  }


  s32 getPlayerInfo(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {

    u8 responseBuffer[512];
    const char postBuffer[1024];
    //spm::mario_pouch::MarioPouchWork* pouch_ptr = spm::mario_pouch::pouchGetPtr();
    snprintf(postBuffer, sizeof(postBuffer), "getPlayerInfo.%d.%d.%d.%s.%s.%d",
      spm::spmario::gp -> gsw[2002],
      spm::spmario::gp -> gsw[2000],
      spm::spmario::gp -> gsw[2001],
      spm::spmario::gp -> saveName,
      spm::spmario::gp -> mapName,
      joiningClients[evtEntry -> lw[4]]
    );
    s32 responseBytes = SendUDP("76.138.196.253", 4000, postBuffer, strlen(postBuffer), responseBuffer, 1024);

    // Ensure data was received
    if (responseBytes > 0) {
        wii::os::OSReport("Response Bytes: %d\n", responseBytes);

        // Ensure we received 12 bytes (3 floats * 4 bytes per float)
        if (responseBytes == 12) {
            for (int i = 0; i < responseBytes; i += 4) {
                // Extract 4 bytes for each float in little-endian order
                u8 byte1 = responseBuffer[i];
                u8 byte2 = responseBuffer[i + 1];
                u8 byte3 = responseBuffer[i + 2];
                u8 byte4 = responseBuffer[i + 3];

                // Combine the bytes into a 32-bit integer (little-endian)
                u32 rawBytes = (byte4 << 24) | (byte3 << 16) | (byte2 << 8) | byte1;

                // Interpret the 32-bit integer as a float
                f32 floatValue;
                memcpy(&floatValue, &rawBytes, sizeof(f32));  // Copy raw bytes into float

                // Log and assign the float value
                evtEntry->lw[(i / 4) + 1] = floatValue;  // Store in lw[] array
                wii::os::OSReport("PlayerPos: %f\n", floatValue);  // Report the float value
            }

            wii::os::OSReport("Number of Players Processed: %d\n", responseBytes / sizeof(f32));
        } else {
            evtEntry->lw[0] = 0;
            wii::os::OSReport("Incorrect number of bytes received. Expected 12.\n");
        }
    } else {
        evtEntry->lw[0] = 0;
        wii::os::OSReport("No response received or an error occurred\n");
    }
    //spm::npcdrv::NPCEntry * otherPlayer = spm::npcdrv::npcEntryFromSetupEnemy(0, &pos, 422, &miscSetupData);
    //spm::evtEntry(otherPlayer->templateUnkScript1, 1, 0x0);
    return 2;
  }

  /*
      General mod functions
  */

  spm::evtmgr::EvtScriptCode * getInstructionEvtArg(spm::evtmgr::EvtScriptCode * script, s32 line, int instruction) {
    spm::evtmgr::EvtScriptCode * link = evtpatch::getEvtInstruction(script, line);
    wii::os::OSReport("%x\n", link);
    spm::evtmgr::EvtScriptCode * arg = evtpatch::getInstructionArgv(link)[instruction];
    wii::os::OSReport("%x\n", arg);
    return arg;
  }

  float getPositionXByClientID(int clientID) {
    for (int i = 0; i < numClients; ++i) {
      if (clients[i].clientID == clientID) {
        return clients[i].positionX;
      }
    }
    return -1.0;
  }

  float getPositionYByClientID(int clientID) {
    for (int i = 0; i < numClients; ++i) {
      if (clients[i].clientID == clientID) {
        return clients[i].positionY;
      }
    }
    return -1.0;
  }

  float getPositionZByClientID(int clientID) {
    for (int i = 0; i < numClients; ++i) {
      if (clients[i].clientID == clientID) {
        return clients[i].positionZ;
      }
    }
    return -1.0;
  }

  const char startText1[] =
    "<system>"
  "You will be registered to\n"
  "SPMMulti. Any online cheating\n"
  "will result in a ban.\n"
  "<k>";

  const char startText2[] =
    "<system>"
  "Registration successful!\n"
  "<k>";

  const char connectionText1[] =
    "<system>"
  "You will be connected to\n"
  "SPMMulti. Any online cheating\n"
  "will result in a ban.\n"
  "<k>";

  const char connectionText2[] =
    "<system>"
  "Connection successful!\n"
  "<k>";

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

EVT_DECLARE_USER_FUNC(startWebhook, 0)
EVT_DECLARE_USER_FUNC(startServerConnection, 0)
EVT_DECLARE_USER_FUNC(checkForPlayersJoiningRoom, 0)
EVT_DECLARE_USER_FUNC(checkConnection, 1)
EVT_DECLARE_USER_FUNC(returnPos, 2)
EVT_DECLARE_USER_FUNC(getPlayerInfo, 0)

EVT_BEGIN(mariounk2)
  SET(LW(0), LW(0))
RETURN_FROM_CALL()

EVT_BEGIN(mariounk7)
  SET(LW(0), LW(0))
RETURN_FROM_CALL()

EVT_BEGIN(registerToServer)
  USER_FUNC(spm::evt_msg::evt_msg_print, 1, PTR(startText1), 0, 0)
  USER_FUNC(startWebhook)
  USER_FUNC(spm::evt_msg::evt_msg_print, 1, PTR(startText2), 0, 0)
RETURN_FROM_CALL()

EVT_BEGIN(evt_connectToServer)
  WAIT_MSEC(1000)
  USER_FUNC(checkConnection, LW(0))
  IF_EQUAL(LW(0), 0)
    USER_FUNC(spm::evt_msg::evt_msg_print, 1, PTR(connectionText1), 0, 0)
    USER_FUNC(startServerConnection)
    USER_FUNC(spm::evt_msg::evt_msg_print, 1, PTR(connectionText2), 0, 0)
    WAIT_MSEC(1000)
    USER_FUNC(checkForPlayersJoiningRoom)
    IF_LARGE(LW(0), 0)
      SET(LW(4), 0)
      DO(0)
        USER_FUNC(getPlayerInfo)
        USER_FUNC(spm::evt_npc::evt_npc_entry_from_template, 0, 422, LW(1), LW(2), LW(3), PTR(0), PTR("me"))
        ADD(LW(4), 1)
        IF_EQUAL(LW(4), LW(0))
          DO_BREAK()
        END_IF()
      WHILE()
    END_IF()
  END_IF()
RETURN_FROM_CALL()

void patchScripts()
{
  spm::map_data::MapData * an1_01_md = spm::map_data::mapDataPtr("aa1_01");
  spm::evtmgr::EvtScriptCode* transition_evt = getInstructionEvtArg(an1_01_md->initScript, 60, 0);
  evtpatch::hookEvt(transition_evt, 10, (spm::evtmgr::EvtScriptCode*)registerToServer);
  spm::map_data::MapData * he1_01_md = spm::map_data::mapDataPtr("he1_01");
  evtpatch::hookEvt(he1_01_md->initScript, 75, (spm::evtmgr::EvtScriptCode*)evt_connectToServer);
  //evtpatch::hookEvtReplaceBlock(spm::npcdrv::npcEnemyTemplates[422].unkScript2, 1, (spm::evtmgr::EvtScriptCode*)mariounk2, 18);
  evtpatch::hookEvtReplace(spm::npcdrv::npcEnemyTemplates[422].unkScript7, 8, (spm::evtmgr::EvtScriptCode*)mariounk7);
}

void main()
{
    wii::os::OSReport("SPM Rel Loader: the mod has ran!\n");
    checkForDolphin();
    romfontExpand();
    exceptionPatch(); // Seeky's exception handler from Practice Codes
    evtDebugPatch();
    evtpatch::evtmgrExtensionInit(); // Initialize EVT scripting extension
    NetMemoryAccess::init();
    //webhookShenanigans();
    titleScreenCustomTextPatch();
    patchScripts();
    //spm::npcdrv::npcEnemyTemplates[422].unkScript2 = mariounk2;
    //tryChainload();
}

}
//USER_FUNC(spm::evt_seq::evt_seq_set_seq, spm::seqdrv::SEQ_MAPCHANGE, PTR("mac_05"), PTR("elv1"))
