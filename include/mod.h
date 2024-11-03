#pragma once
#include "evt_cmd.h"
#include "core_http_client.h"

namespace mod {

#define MOD_VERSION "SPM-Multi"

extern bool gIsDolphin;
extern bool isConnected;
extern const char startText1[];
extern const char startText2[];
extern const char connectionText1[];
extern const char connectionText2[];

EVT_DECLARE(mariounk3)
EVT_DECLARE(mariounk6)
EVT_DECLARE(playerMainLogic)
EVT_DECLARE(registerToServer)
EVT_DECLARE(evt_connectToServer)
EVT_DECLARE(evt_spawn_players)
EVT_DECLARE(thunderCloud)
EVT_DECLARE(spawnThunderCloud)
EVT_DECLARE(insertNop)
EVT_DECLARE(setResults)
EVT_DECLARE(patchBoxes)
EVT_DECLARE(fixName)
extern spm::evtmgr::EvtScriptCode* thunderRageScript;

u16 getMotionIdByClientID(int clientID);
void setMotionIdByClientID(int clientID, u16 motionId);
float getPositionXByClientID(int clientID);
float getPositionYByClientID(int clientID);
bool getDCByClientID(int clientID);
void removePlayer(int clientID);
HTTPStatus_t registerPlayer();
HTTPStatus_t connectToServer();
void main();

}
