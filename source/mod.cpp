#include "mod.h"
#include "evt_cmd.h"
#include "stdlib.h"
#include "commandmanager.h"
#include "patch.h"
#include "netmemoryaccess.h"
#include "network.h"
#include "spmhttp.h"
#include "core_json.h"
#include "chainloader.h"
#include "cutscene_helpers.h"
#include "evtpatch.h"
#include "exception.h"
#include "evtdebug.h"
#include "romfontexpand.h"
#include "errno.h"

#include <spm/setup_data.h>
#include <spm/item_event_data.h>
#include <spm/npcdrv.h>
#include <spm/seq_mapchange.h>
#include <spm/evt_seq.h>
#include <spm/evt_eff.h>
#include <spm/evt_snd.h>
#include <spm/evt_msg.h>
#include <spm/evtmgr.h>
#include <spm/map_data.h>
#include <spm/fontmgr.h>
#include <spm/seqdrv.h>
#include <spm/seqdef.h>
#include <spm/seq_game.h>
#include <spm/spmario.h>
#include <spm/mario.h>
#include <spm/mario_pouch.h>
#include <spm/system.h>
#include <wii/os/OSError.h>
#include <wii/os/OSThread.h>
#include <wii/ipc.h>
#include <wii/vi.h>
#include <wii/os.h>
#include <msl/stdio.h>
#include <msl/string.h>

namespace mod {
  bool gIsDolphin;
  bool tfirstRun = false;
  bool isConnected = false;
  spm::evtmgr::EvtScriptCode* thunderRageScript = spm::item_event_data::itemEventDataTable[2].useScript;
  u32 sockfd;

  struct Player {
    int clientID;
    float positionX;
    float positionY;
    float positionZ;
    int attack;
    int maxHP;
    int currentHP;
    bool isDead;
    bool isDisconnected;
    u16 motionId;

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
  int numOfClients = 0;

  // Function to add a new Player to clients array
  void addPlayer(int clientID, float positionX, float positionY, float positionZ, int attack, int maxHP, int currentHP) {
      if (numOfClients < 24) { // Make sure there's space in the array
          Player newPlayer;
          newPlayer.clientID = clientID;
          newPlayer.positionX = positionX;
          newPlayer.positionY = positionY;
          newPlayer.positionZ = positionZ;
          newPlayer.attack = attack;
          newPlayer.maxHP = maxHP;
          newPlayer.currentHP = currentHP;
          newPlayer.isDead = (currentHP <= 0);
          newPlayer.isDisconnected = false;
          newPlayer.motionId = 0x00;

          clients[numOfClients] = newPlayer;  // Add the new player to the array
          numOfClients++;  // Increment the number of clients

          wii::os::OSReport("Player added with clientID: %d\n", clientID);
      } else {
          wii::os::OSReport("No space left to add a new player.\n");
      }
  }

  // Function to remove a Player from clients array
  void removePlayer(int clientID) {
      bool found = false;

      for (int i = 0; i < numOfClients; ++i) {
          if (clients[i].clientID == clientID) {
              found = true;
              clients[i].clientID = 0;
              // Shift subsequent players down to fill the gap
              for (int j = i; j < numOfClients - 1; ++j) {
                  clients[j] = clients[j + 1];
              }
              numOfClients--;  // Decrement the number of clients
              wii::os::OSReport("Player removed with clientID: %d\n", clientID);
              break;
          }
      }

      if (!found) {
      wii::os::OSReport("Player with clientID %d not found\n", clientID);
      }
  }

  void removeAllPlayers() {

     for (int i = 0; i < numOfClients; ++i) {
       if (clients[i].clientID != 0) {
         removePlayer(clients[i].clientID);
      }
    }
      wii::os::OSReport("All players have been removed.\n");
  }

  // Function to check if a Player is in the clients array
  bool isPlayerInClients(int clientID) {
      for (int i = 0; i < numOfClients; ++i) {
          if (clients[i].clientID == clientID) {
              return true;
          }
      }
      return false;
  }

  u16 getMotionIdByClientID(int clientID) {
    for (int i = 0; i < numOfClients; ++i) {
      if (clients[i].clientID == clientID) {
        return clients[i].motionId;
      }
    }
    return 0x00;
  }

  void setMotionIdByClientID(int clientID, u16 motionId) {
    for (int i = 0; i < numOfClients; ++i) {
      if (clients[i].clientID == clientID) {
        clients[i].motionId = motionId;
      }
    }
  }

  float getPositionXByClientID(int clientID) {
    for (int i = 0; i < numOfClients; ++i) {
      if (clients[i].clientID == clientID) {
        return clients[i].positionX;
      }
    }
    return -1.0;
  }

  float getPositionYByClientID(int clientID) {
    for (int i = 0; i < numOfClients; ++i) {
      if (clients[i].clientID == clientID) {
        return clients[i].positionY;
      }
    }
    return -1.0;
  }

  float getPositionZByClientID(int clientID) {
    for (int i = 0; i < numOfClients; ++i) {
      if (clients[i].clientID == clientID) {
        return clients[i].positionZ;
      }
    }
    return -1.0;
  }

  bool getDCByClientID(int clientID) {
    for (int i = 0; i < numOfClients; ++i) {
      if (clients[i].clientID == clientID) {
        return clients[i].isDisconnected;
      }
    }
    return -1.0;
  }

    const int MAX_TOKENS = 100;
    s32 joiningClients[MAX_TOKENS];
    s32 joiningClientsNum = 0;
    f32 joiningClientsPos[3];
    bool checkingForPlayers = true;
    bool sockIsCreated = false;
  /// @brief Sends a UDP packet to the server and waits for a response
  /// @param host The host (e.g., "192.168.1.1")
  /// @param port The port (e.g., 8080)
  /// @param data The data to send
  /// @param dataSize Size of the data buffer
  /// @param responseBuffer A buffer to store the server's response
  /// @param responseBufferSize Size of the response buffer
  /// @return The number of bytes received in the response or a negative error code
  s32 SendUDP(const char* host, int port, const u8* data, size_t dataSize, u8* responseBuffer, size_t responseBufferSize) {
      struct sockaddr_in serv_addr, recv_addr;
      struct hostent* server;
      socklen_t recv_addr_len = sizeof(recv_addr);

      if (!sockfd || sockfd < 0) {
        sockfd = Mynet_socket(AF_INET, SOCK_DGRAM, 0);  // Use SOCK_DGRAM for UDP

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 200;

        int ret = Mynet_setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        wii::os::OSReport("Socket timeout ret: %d\n", ret);
      }

      if (sockfd < 0) {
          wii::os::OSReport("ERROR opening UDP socket\n");
          return -1;
      }

      server = Mynet_gethostbyname((char*)host);
      if (server == NULL) {
          wii::os::OSReport("ERROR no such host\n");
          Mynet_close(sockfd);
          return -1;
      }

      msl::string::memset(&serv_addr, 0, sizeof(serv_addr));
      serv_addr.sin_family = AF_INET;
      serv_addr.sin_port = htons(port);
      msl::string::memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);

      // Send the UDP data
      s32 sentBytes = Mynet_sendto(sockfd, data, dataSize, 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
      if (sentBytes < 0) {
          wii::os::OSReport("ERROR sending UDP packet\n");
          Mynet_close(sockfd);
          return -1;
      }

      //wii::os::OSReport("UDP packet sent successfully\n");

      // Wait for a response from the server
      s32 recvBytes = Mynet_recvfrom(sockfd, responseBuffer, responseBufferSize, 0, (struct sockaddr*)&recv_addr, &recv_addr_len);
      if (recvBytes < 0) {
          wii::os::OSReport("ERROR receiving UDP response\n");
          Mynet_close(sockfd);
          return -1;
      }

      //wii::os::OSReport("UDP response received: %d bytes\n", recvBytes);

      return recvBytes;
  }

  u8 stack[STACK_SIZE];
  wii::os::OSThread thread;
  s32 timerLimit = 0;
  s32 checkForPlayersTimer = 0;
  s32 updateStatsTimer = 0;
  bool leavingMap = false;


  void spmServerLoop(u32 param) {
    (void) param;
    while (1) {
      timerLimit += 1;
      checkForPlayersTimer += 1;
      updateStatsTimer += 1;
      if (timerLimit >= 3){
      timerLimit = 0;
      bool varCheck = true;
      if (varCheck == true) { //update position

        u8 responseBuffer[1024];
        const char postBuffer[1024];

        spm::mario::MarioWork * mwpp = spm::mario::marioGetPtr();
        spm::mario_pouch::MarioPouchWork * pouch_ptr = spm::mario_pouch::pouchGetPtr();
        wii::mtx::Vec3 pos = mwpp -> position;
        u16 motionId = mwpp -> motionId;
        if (mwpp -> posLockTimer > 0.0) {
          motionId = 5000;
        }
        if (pouch_ptr -> hp == 0) {
          motionId = 6000;
        }
        snprintf(postBuffer, sizeof(postBuffer), "updatePosition.%d.%d.%d.%s.%d.%d.%d.%s.%d",
            spm::spmario::gp->gsw[2002],
            spm::spmario::gp->gsw[2000],
            spm::spmario::gp->gsw[2001],
            spm::spmario::gp->saveName,
            roundi(pos.x * 1000),
            roundi(pos.y * 1000),
            roundi(pos.z * 1000),
            spm::spmario::gp->mapName,
            motionId
        );

        s32 responseBytes = SendUDP("76.138.196.253", 4000, postBuffer, strlen(postBuffer), responseBuffer, 1024);

        // Ensure data was received
        if (responseBytes > 0) {
            wii::os::OSReport("Response Bytes: %d\n", responseBytes);
        } else {
            wii::os::OSReport("No response received or an error occurred\n");
        }

      }
      if (checkForPlayersTimer > 60) { //check for players
        checkingForPlayers = false;
        checkForPlayersTimer = 0;

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

            s32 realClientIDs[numClients];
            s32 realNumClients = numClients;
            s32 secondaryIndex = 0;
            wii::os::OSReport("Number of Clients: %d\n", numClients);  // Log the number of integers (clients)

            if (numClients > 0) {

              for (int i = 0; i < numClients; ++i) {
                if (!isPlayerInClients(static_cast<s32>(clientIDs[i]))) {
                  realClientIDs[secondaryIndex] = static_cast<s32>(clientIDs[i]);  // Assign the converted client ID
                  secondaryIndex += 1;
                } else {
                  realNumClients -= 1;
                }
              }

              // if there are 0 or negative clients to load, set it to 0 so that no NPC is spawned and do nothing
              if (realNumClients <= 0) {
                realNumClients = 0;
                checkingForPlayers = true;
              } else {
                for (int i = 0; i < realNumClients; ++i) {
                    joiningClients[i] = realClientIDs[i];
                    wii::os::OSReport("ClientID: %d\n", realClientIDs[i]);  // Report the client ID
                }

              }
                //evtEntry->lw[0] = realNumClients;
                joiningClientsNum = realNumClients;
                wii::os::OSReport("Total Clients Processed: %d\n", realNumClients);
            } else {
                wii::os::OSReport("No valid clients found.\n");
                checkingForPlayers = true;
            }
        } else {
            wii::os::OSReport("No response received or an error occurred\n");
            checkingForPlayers = true;
        }
      }
      if (joiningClientsNum >= 1) {

            for (int i = 0; i < joiningClientsNum; ++i) {


            s32 playerStats[3]; // Array to store the three integers (attack, maxHP, currentHP)
            f32 posArray[3]; //Array to store position
            u8 responseBuffer[512];
            const char postBuffer[1024];

            snprintf(postBuffer, sizeof(postBuffer), "getPlayerInfo.%d.%d.%d.%s.%s.%d",
              spm::spmario::gp -> gsw[2002],
              spm::spmario::gp -> gsw[2000],
              spm::spmario::gp -> gsw[2001],
              spm::spmario::gp -> saveName,
              spm::spmario::gp -> mapName,
              joiningClients[i] // Replace any instance of evtEntry -> lw[4] with i
            );
            s32 responseBytes = SendUDP("76.138.196.253", 4000, postBuffer, strlen(postBuffer), responseBuffer, 1024);

            // Ensure data was received
            if (responseBytes > 0) {
                wii::os::OSReport("Response Bytes: %d\n", responseBytes);

                // We expect 12 bytes (3 integers * 4 bytes per integer)
                if (responseBytes == 12) {

                    for (int i = 0; i < responseBytes; i += 4) {
                        // Extract 4 bytes for each integer in little-endian order
                        u8 byte1 = responseBuffer[i];
                        u8 byte2 = responseBuffer[i + 1];
                        u8 byte3 = responseBuffer[i + 2];
                        u8 byte4 = responseBuffer[i + 3];

                        // Combine the bytes into a 32-bit integer (little-endian)
                        s32 value = (byte4 << 24) | (byte3 << 16) | (byte2 << 8) | byte1;

                        // Assign the integer to the playerStats array
                        playerStats[i / 4] = value;

                        // Log the integer value
                        wii::os::OSReport("PlayerStat[%d]: %d\n", i / 4, value);
                    }
                    wii::os::OSReport("All player stats processed.\n");

                    // Player position MUST be stored in LW 1, 2, and 3 before this function is ran
                    //addPlayer(joiningClients[evtEntry -> lw[4]], evtEntry -> lw[1], evtEntry -> lw[2], evtEntry -> lw[3], playerStats[0], playerStats[1], playerStats[2]);
                    //evtEntry -> lw[5] = playerStats[0];
                    //evtEntry -> lw[6] = playerStats[2];
                    //evtEntry -> lw[7] = joiningClients[evtEntry -> lw[4]];
                } else {
                    wii::os::OSReport("Unexpected response size: %d\n", responseBytes);
                }
            } else {
                wii::os::OSReport("No response received or an error occurred.\n");
            }

            u8 responseBuffer1[512];
            snprintf(postBuffer, sizeof(postBuffer), "getPlayerPos.%d.%d.%d.%s.%s.%d",
              spm::spmario::gp -> gsw[2002],
              spm::spmario::gp -> gsw[2000],
              spm::spmario::gp -> gsw[2001],
              spm::spmario::gp -> saveName,
              spm::spmario::gp -> mapName,
              joiningClients[i]
            );
            responseBytes = SendUDP("76.138.196.253", 4000, postBuffer, strlen(postBuffer), responseBuffer1, 1024);

            // Ensure data was received
            if (responseBytes > 0) {
                wii::os::OSReport("Response Bytes: %d\n", responseBytes);

                // Ensure we received 12 bytes (3 floats * 4 bytes per float)
                if (responseBytes == 12) {
                    for (int i = 0; i < responseBytes; i += 4) {
                        // Extract 4 bytes for each float in little-endian order
                        u8 byte1 = responseBuffer1[i];
                        u8 byte2 = responseBuffer1[i + 1];
                        u8 byte3 = responseBuffer1[i + 2];
                        u8 byte4 = responseBuffer1[i + 3];

                        // Combine the bytes into a 32-bit integer (little-endian)
                        u32 rawBytes = (byte4 << 24) | (byte3 << 16) | (byte2 << 8) | byte1;

                        // Interpret the 32-bit integer as a float
                        f32 floatValue;
                        memcpy(&floatValue, &rawBytes, sizeof(f32));  // Copy raw bytes into float

                        // Log and assign the float value
                        posArray[(i / 4)] = floatValue;
                        wii::os::OSReport("PlayerPos: %f\n", floatValue);  // Report the float value
                    }

                    wii::os::OSReport("Number of Players Processed: %d\n", responseBytes / sizeof(f32));
                } else {
                    wii::os::OSReport("Incorrect number of bytes received. Expected 12.\n");
                }
            } else {
                wii::os::OSReport("No response received or an error occurred\n");
            }
            addPlayer(joiningClients[i], posArray[0], posArray[1], posArray[2], playerStats[0], playerStats[1], playerStats[2]);
            spm::evtmgr::EvtEntry * evtEntry = spm::evtmgr::evtEntry(evt_spawn_players, 1, 0x0);
            wii::os::OSReport("Evtentry created ez\n");
            evtEntry->lw[1] = posArray[0];
            evtEntry->lw[2] = posArray[1];
            evtEntry->lw[3] = posArray[2];
            evtEntry->lw[5] = playerStats[0];
            evtEntry->lw[6] = playerStats[2];
            evtEntry->lw[7] = joiningClients[i];
            checkingForPlayers = true;
          }
          joiningClientsNum = 0;
      }
      if (numOfClients >= 1) {
        for (int i = 0; i < numOfClients; ++i) {
          u8 responseBuffer[512];
          const char postBuffer[1024];
          snprintf(postBuffer, sizeof(postBuffer), "getPlayerPos.%d.%d.%d.%s.%s.%d",
            spm::spmario::gp -> gsw[2002],
            spm::spmario::gp -> gsw[2000],
            spm::spmario::gp -> gsw[2001],
            spm::spmario::gp -> saveName,
            spm::spmario::gp -> mapName,
            clients[i].clientID
          );
          s32 responseBytes = SendUDP("76.138.196.253", 4000, postBuffer, strlen(postBuffer), responseBuffer, 1024);

          // Ensure data was received
          if (responseBytes > 0) {
              wii::os::OSReport("Response Bytes: %d\n", responseBytes);
              f32 posArray[3];
              u16 motionId;

              // Ensure we received 14 bytes (3 floats * 4 bytes per float + 1 u16 * 2 bytes)
              if (responseBytes == 14) {
                  // Extract the position floats (first 12 bytes)
                  for (int i = 0; i < 12; i += 4) {
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
                      posArray[(i / 4)] = floatValue;  // Store in posArray
                      wii::os::OSReport("PlayerPos: %f\n", floatValue);  // Report the float value
                  }

                  // Extract the motionId (last 2 bytes, bytes 12 and 13)
                  u8 byteMotion1 = responseBuffer[12];
                  u8 byteMotion2 = responseBuffer[13];

                  // Combine the bytes into a 16-bit integer (little-endian)
                  motionId = (byteMotion2 << 8) | byteMotion1;
                  clients[i].motionId = motionId;

                  // Update client position
                  clients[i].positionX = posArray[0];
                  clients[i].positionY = posArray[1];
                  clients[i].positionZ = posArray[2];

                  // Handle disconnection condition based on position
                  if (posArray[0] == 0.0f && posArray[1] == 0.0f && posArray[2] == 0.0f) {
                      clients[i].isDisconnected = true;
                      removePlayer(clients[i].clientID);
                  }

                  wii::os::OSReport("Number of Players Processed: %d\n", responseBytes / sizeof(f32));
              } else {
                  wii::os::OSReport("Incorrect number of bytes received. Expected 14.\n");
                  clients[i].isDisconnected = true;
              }
          } else {
              wii::os::OSReport("No response received or an error occurredeww\n");
              clients[i].isDisconnected = true;
          }

        }
      }
      if (updateStatsTimer >= 60) {
        updateStatsTimer = 0;
        u8 responseBuffer[1024];
        const char postBuffer[1024];

        spm::mario_pouch::MarioPouchWork * pouch_ptr = spm::mario_pouch::pouchGetPtr();
        snprintf(postBuffer, sizeof(postBuffer), "updateStats.%d.%d.%d.%s.%d.%d.%d.%d.%d",
          spm::spmario::gp -> gsw[2002],
          spm::spmario::gp -> gsw[2000],
          spm::spmario::gp -> gsw[2001],
          spm::spmario::gp -> saveName,
          pouch_ptr -> hp,
          pouch_ptr -> maxHp,
          pouch_ptr -> attack,
          pouch_ptr -> xp,
          spm::spmario::gp -> gsw0
        );

        s32 responseBytes = SendUDP("76.138.196.253", 4000, postBuffer, strlen(postBuffer), responseBuffer, 1024);

        // Ensure data was received
        if (responseBytes > 0) {
          wii::os::OSReport("Response Bytes: %d\n", responseBytes);
        } else {
          wii::os::OSReport("No response received or an error occurred\n");
        }

      }
      wii::os::OSYieldThread();
      wii::vi::VIWaitForRetrace();
    }}
  }

  void spmServerInit() {
      wii::os::OSCreateThread(&thread, (wii::os::ThreadFunc*)spmServerLoop, 0, stack + STACK_SIZE, STACK_SIZE, 24, 1);
      wii::os::OSResumeThread(&thread);
  }

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
    spmServerInit();
    return mystatus;
  }

  s32 npcGetPlayerPos(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {
    spm::npcdrv::NPCEntry * ownerNpc = (spm::npcdrv::NPCEntry *)evtEntry -> ownerNPC;
    s32 leClient = ownerNpc -> unitWork[0];

    evtEntry->lw[1] = getPositionXByClientID(leClient);
    evtEntry->lw[2] = getPositionYByClientID(leClient);
    evtEntry->lw[3] = getPositionZByClientID(leClient);
    return 2;
  }

  s32 npcFixAnims(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {
    spm::npcdrv::NPCEntry * ownerNpc = (spm::npcdrv::NPCEntry *)evtEntry -> ownerNPC;
    spm::npcdrv::func_801ca1a4(ownerNpc, &ownerNpc -> m_Anim);
    return 2;
}
  /*
      General mod functions
  */

  void (*seq_gameExit)(spm::seqdrv::SeqWork *param_1);
  void patchGameExit() {
  seq_gameExit = patch::hookFunction(spm::seq_game::seq_gameExit,
  [](spm::seqdrv::SeqWork *param_1)
      {
          Mynet_close(sockfd);
          seq_gameExit(param_1);
      }
  );
  }

  void (*seq_mapChangeInit)(spm::seqdrv::SeqWork *param_1);
  void patchMapInit() {
  seq_mapChangeInit = patch::hookFunction(spm::seq_mapchange::seq_mapChangeInit,
  [](spm::seqdrv::SeqWork *param_1)
      {
          seq_mapChangeInit(param_1);
          removeAllPlayers();
      }
  );
}

  spm::evtmgr::EvtScriptCode * getInstructionEvtArg(spm::evtmgr::EvtScriptCode * script, s32 line, int instruction) {
    spm::evtmgr::EvtScriptCode * link = evtpatch::getEvtInstruction(script, line);
    wii::os::OSReport("%x\n", link);
    spm::evtmgr::EvtScriptCode * arg = evtpatch::getInstructionArgv(link)[instruction];
    wii::os::OSReport("%x\n", arg);
    return arg;
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



void patchScripts()
{
  spm::map_data::MapData * an1_01_md = spm::map_data::mapDataPtr("aa1_01");
  spm::evtmgr::EvtScriptCode* transition_evt = getInstructionEvtArg(an1_01_md->initScript, 60, 0);
  evtpatch::hookEvt(transition_evt, 10, (spm::evtmgr::EvtScriptCode*)registerToServer);
  spm::map_data::MapData * he1_01_md = spm::map_data::mapDataPtr("he1_01");
  evtpatch::hookEvt(he1_01_md->initScript, 75, (spm::evtmgr::EvtScriptCode*)evt_connectToServer);
  evtpatch::hookEvt(spm::npcdrv::npcEnemyTemplates[422].unkScript6, 1, (spm::evtmgr::EvtScriptCode*)mariounk6);
  evtpatch::hookEvt(spm::npcdrv::npcEnemyTemplates[422].unkScript3, 71, (spm::evtmgr::EvtScriptCode*)mariounk3);
}

void patchMario()
{
  spm::npcdrv::npcEnemyTemplates[422].unkScript7 = playerMainLogic;
  spm::npcdrv::npcEnemyTemplates[422].unkScript2 = playerMainLogic;
  spm::npcdrv::npcTribes[453].killXp = 0;
  spm::npcdrv::npcTribes[453].animDefs[5] = {6, "mario_D_1"};
}

void patchItems()
{
  //evtpatch::hookEvtReplaceBlock(spm::item_event_data::itemEventDataTable[2].useScript, 1, (spm::evtmgr::EvtScriptCode*)spawnThunderCloud, 62);
  evtpatch::hookEvtReplaceBlock(thunderRageScript, 2, (spm::evtmgr::EvtScriptCode*)fixName, 33);
  evtpatch::hookEvtReplace(thunderRageScript, 1, (spm::evtmgr::EvtScriptCode*)setResults);
  //evtpatch::hookEvtReplace(thunderRageScript, 20, (spm::evtmgr::EvtScriptCode*)insertNop);
  //evtpatch::hookEvtReplace(thunderRageScript, 33, (spm::evtmgr::EvtScriptCode*)fixName);
  //evtpatch::hookEvtReplaceBlock(thunderRageScript, 15, (spm::evtmgr::EvtScriptCode*)insertNop, 21);
  //evtpatch::hookEvt(spm::item_event_data::itemEventDataTable[2].useScript, 62, (spm::evtmgr::EvtScriptCode*)spawnThunderCloud);
  spm::item_event_data::itemEventDataTable[2].useScript = spawnThunderCloud;
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
    titleScreenCustomTextPatch();
    patchScripts();
    patchMapInit();
    patchMario();
    //patchGameExit();
    //tryChainload();
    patchItems();
}

}
