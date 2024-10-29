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

float getPositionXByClientID(int clientID);
float getPositionYByClientID(int clientID);
bool getDCByClientID(int clientID);
void removePlayer(int clientID);
HTTPStatus_t registerPlayer();
HTTPStatus_t connectToServer();
void main();

}
