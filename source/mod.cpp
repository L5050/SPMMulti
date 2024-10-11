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
          clients[i].clientID = 0;
      }
      numOfClients = 0;

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
    s32 checkForPlayersTimer = 0;

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
      int32_t sentBytes = Mynet_sendto(sockfd, data, dataSize, 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
      if (sentBytes < 0) {
          wii::os::OSReport("ERROR sending UDP packet\n");
          Mynet_close(sockfd);
          return -1;
      }

      wii::os::OSReport("UDP packet sent successfully\n");

      // Wait for a response from the server
      s32 recvBytes = Mynet_recvfrom(sockfd, responseBuffer, responseBufferSize, 0, (struct sockaddr*)&recv_addr, &recv_addr_len);
      if (recvBytes < 0) {
          wii::os::OSReport("ERROR receiving UDP response\n");
          Mynet_close(sockfd);
          return -1;
      }

      wii::os::OSReport("UDP response received: %d bytes\n", recvBytes);

      return recvBytes;
  }

  u8 stack[STACK_SIZE];
  wii::os::OSThread thread;
  s32 timerLimit = 0;
  bool leavingMap = false;


  void spmServerLoop(u32 param) {
    (void) param;
    while (1) {
      timerLimit += 1;
      checkForPlayersTimer += 1;
      if (timerLimit >= 11){
      timerLimit = 0;
      bool varCheck = true;
      if (varCheck == true) { //update position

        u8 responseBuffer[1024];
        const char postBuffer[1024];

        spm::mario::MarioWork * mwpp = spm::mario::marioGetPtr();
        wii::mtx::Vec3 pos = mwpp -> position;
        snprintf(postBuffer, sizeof(postBuffer), "updatePosition.%d.%d.%d.%s.%d.%d.%d.%s",
            spm::spmario::gp->gsw[2002],
            spm::spmario::gp->gsw[2000],
            spm::spmario::gp->gsw[2001],
            spm::spmario::gp->saveName,
            roundi(pos.x * 1000),
            roundi(pos.y * 1000),
            roundi(pos.z * 1000),
            spm::spmario::gp->mapName
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
                      posArray[(i / 4)] = floatValue;  // Store in lw[] array
                      wii::os::OSReport("PlayerPos: %f\n", floatValue);  // Report the float value
                  }

                  wii::os::OSReport("Number of Players Processede: %d\n", responseBytes / sizeof(f32));
              } else {
                  wii::os::OSReport("Incorrect number of bytes received. Expected 12.\n");
                  clients[i].isDisconnected = true;
              }
              clients[i].positionX = posArray[0];
              clients[i].positionY = posArray[1];
              clients[i].positionZ = posArray[2];
              if (posArray[0] == 0.0 && posArray[1] == 0.0 && posArray[2] == 0.0) {
                clients[i].isDisconnected = true;
              }
          } else {
              wii::os::OSReport("No response received or an error occurredeww\n");
              clients[i].isDisconnected = true;
          }

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
            } else {
              for (int i = 0; i < realNumClients; ++i) {
                  joiningClients[i] = realClientIDs[i];
                  wii::os::OSReport("ClientID: %d\n", realClientIDs[i]);  // Report the client ID
              }
            }
              evtEntry->lw[0] = realNumClients;
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

  s32 updateServerPos(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {
      u8 responseBuffer[1024];
      const char postBuffer[1024];

      spm::mario::MarioWork * mwpp = spm::mario::marioGetPtr();
      wii::mtx::Vec3 pos = mwpp -> position;
      snprintf(postBuffer, sizeof(postBuffer), "updatePosition.%d.%d.%d.%s.%d.%d.%d",
          spm::spmario::gp->gsw[2002],
          spm::spmario::gp->gsw[2000],
          spm::spmario::gp->gsw[2001],
          spm::spmario::gp->saveName,
          roundi(pos.x * 1000),
          roundi(pos.y * 1000),
          roundi(pos.z * 1000)
      );

      s32 responseBytes = SendUDP("76.138.196.253", 4000, postBuffer, strlen(postBuffer), responseBuffer, 1024);

      // Ensure data was received
      if (responseBytes > 0) {
          wii::os::OSReport("Response Bytes: %d\n", responseBytes);
      } else {
          wii::os::OSReport("No response received or an error occurred\n");
      }

      return 2;
  }

  s32 getPlayerPos(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {

    u8 responseBuffer[512];
    const char postBuffer[1024];
    //spm::mario_pouch::MarioPouchWork* pouch_ptr = spm::mario_pouch::pouchGetPtr();
    snprintf(postBuffer, sizeof(postBuffer), "getPlayerPos.%d.%d.%d.%s.%s.%d",
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

  s32 npcGetPlayerPos(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {
    spm::npcdrv::NPCEntry * ownerNpc = (spm::npcdrv::NPCEntry *)evtEntry -> ownerNPC;
    s32 leClient = ownerNpc -> unitWork[0];
    /*
    u8 responseBuffer[512];
    const char postBuffer[1024];
    spm::npcdrv::NPCEntry * ownerNpc = (spm::npcdrv::NPCEntry *)evtEntry -> ownerNPC;
    snprintf(postBuffer, sizeof(postBuffer), "getPlayerPos.%d.%d.%d.%s.%s.%d",
      spm::spmario::gp -> gsw[2002],
      spm::spmario::gp -> gsw[2000],
      spm::spmario::gp -> gsw[2001],
      spm::spmario::gp -> saveName,
      spm::spmario::gp -> mapName,
      ownerNpc -> unitWork[0]
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
    //spm::evtEntry(otherPlayer->templateUnkScript1, 1, 0x0); */
    evtEntry->lw[1] = getPositionXByClientID(leClient);
    evtEntry->lw[2] = getPositionYByClientID(leClient);
    evtEntry->lw[3] = getPositionZByClientID(leClient);
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

        // We expect 12 bytes (3 integers * 4 bytes per integer)
        if (responseBytes == 12) {
            s32 playerStats[3];  // Array to store the three integers (attack, maxHP, currentHP)

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
            addPlayer(joiningClients[evtEntry -> lw[4]], evtEntry -> lw[1], evtEntry -> lw[2], evtEntry -> lw[3], playerStats[0], playerStats[1], playerStats[2]);
            evtEntry -> lw[5] = playerStats[0];
            evtEntry -> lw[6] = playerStats[2];
            evtEntry -> lw[7] = joiningClients[evtEntry -> lw[4]];
        } else {
            wii::os::OSReport("Unexpected response size: %d\n", responseBytes);
        }
    } else {
        wii::os::OSReport("No response received or an error occurred.\n");
    }

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
          removeAllPlayers();
          seq_mapChangeInit(param_1);
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
    if (evtEntry -> lw[1] == evtEntry -> lw[5] && evtEntry -> lw[2] == evtEntry -> lw[6] && evtEntry -> lw[3] == evtEntry -> lw[6])
    {
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
      evtEntry -> lw[8] = speed;
      return 2;
}

EVT_DECLARE_USER_FUNC(startWebhook, 0)
EVT_DECLARE_USER_FUNC(startServerConnection, 0)
EVT_DECLARE_USER_FUNC(checkForPlayersJoiningRoom, 0)
EVT_DECLARE_USER_FUNC(checkConnection, 1)
EVT_DECLARE_USER_FUNC(returnPos, 2)
EVT_DECLARE_USER_FUNC(getPlayerPos, 0)
EVT_DECLARE_USER_FUNC(updateServerPos, 0)
EVT_DECLARE_USER_FUNC(npcGetPlayerPos, 0)
EVT_DECLARE_USER_FUNC(npcFixAnims, 0)
EVT_DECLARE_USER_FUNC(getPlayerInfo, 0)
EVT_DECLARE_USER_FUNC(checkPosEqual, 0)
EVT_DECLARE_USER_FUNC(npcDeletePlayer, 0)
EVT_DECLARE_USER_FUNC(adjustJumpSpeed, 0)
EVT_DECLARE_USER_FUNC(checkPlayerDC, 0)

EVT_BEGIN(mariounk2)
  SET(LW(0), LW(0))
RETURN_FROM_CALL()

EVT_BEGIN(mariounk6)
  USER_FUNC(npcDeletePlayer)
RETURN_FROM_CALL()

EVT_BEGIN(mariounk7)
  USER_FUNC(npcGetPlayerPos)
  USER_FUNC(spm::evt_npc::evt_npc_get_position, PTR("me"), LW(5), LW(6), LW(7))
  USER_FUNC(checkPosEqual)
  IF_EQUAL(LW(8), 0)
    SET(LW(8), LW(2))
    SUB(LW(8), LW(6))
    IF_LARGE(LW(8), LW(6))
      USER_FUNC(spm::evt_snd::evt_snd_sfxon_npc, PTR("SFX_P_MARIO_JUMP1"), PTR("me"))
      USER_FUNC(npcFixAnims)
      USER_FUNC(spm::evt_npc::evt_npc_set_anim, PTR("me"), 0x19, 0)
      USER_FUNC(adjustJumpSpeed)
      USER_FUNC(spm::evt_npc::evt_npc_jump_to, PTR("me"), LW(1), LW(2), LW(3), FLOAT(10.0), LW(8))
      USER_FUNC(spm::evt_snd::evt_snd_sfxon_npc, PTR("SFX_P_MARIO_LAND1"), PTR("me"))
      USER_FUNC(npcFixAnims)
      USER_FUNC(spm::evt_npc::evt_npc_set_anim, PTR("me"), 0, 0)
    ELSE()
      USER_FUNC(npcFixAnims)
      USER_FUNC(spm::evt_npc::evt_npc_set_anim, PTR("me"), 2, 0)
      USER_FUNC(spm::evt_npc::evt_npc_walk_to, PTR("me"), LW(1), LW(3), 0, FLOAT(180.0), 4, 0, 0)
      USER_FUNC(npcFixAnims)
      USER_FUNC(spm::evt_npc::evt_npc_set_anim, PTR("me"), 0, 0)
    END_IF()
  END_IF()
  USER_FUNC(checkPlayerDC)
  IF_EQUAL(LW(10), 1)
    USER_FUNC(spm::evt_npc::evt_npc_delete, PTR("me"))
  END_IF()
  WAIT_FRM(12)
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
  END_IF() /*
  DO(0)
    USER_FUNC(checkForPlayersJoiningRoom)
    IF_LARGE(LW(0), 0)
      SET(LW(4), 0)
      DO(0)
        USER_FUNC(getPlayerPos)
        USER_FUNC(getPlayerInfo)
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
      WHILE()
    END_IF()
    WAIT_MSEC(500)
    USER_FUNC(updateServerPos)
  WHILE()*/
RETURN_FROM_CALL()

EVT_BEGIN(evt_spawn_players)
    //USER_FUNC(getPlayerPos)
    //USER_FUNC(getPlayerInfo)
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

void patchScripts()
{
  spm::map_data::MapData * an1_01_md = spm::map_data::mapDataPtr("aa1_01");
  spm::evtmgr::EvtScriptCode* transition_evt = getInstructionEvtArg(an1_01_md->initScript, 60, 0);
  evtpatch::hookEvt(transition_evt, 10, (spm::evtmgr::EvtScriptCode*)registerToServer);
  spm::map_data::MapData * he1_01_md = spm::map_data::mapDataPtr("he1_01");
  evtpatch::hookEvt(he1_01_md->initScript, 75, (spm::evtmgr::EvtScriptCode*)evt_connectToServer);
  //evtpatch::hookEvtReplaceBlock(spm::npcdrv::npcEnemyTemplates[422].unkScript2, 1, (spm::evtmgr::EvtScriptCode*)mariounk2, 18);
  evtpatch::hookEvtReplace(spm::npcdrv::npcEnemyTemplates[422].unkScript7, 8, (spm::evtmgr::EvtScriptCode*)mariounk7);
  evtpatch::hookEvt(spm::npcdrv::npcEnemyTemplates[422].unkScript6, 1, (spm::evtmgr::EvtScriptCode*)mariounk6);
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
    //patchGameExit();
    //spm::npcdrv::npcEnemyTemplates[422].unkScript2 = mariounk2;
    //tryChainload();
}

}
//USER_FUNC(spm::evt_seq::evt_seq_set_seq, spm::seqdrv::SEQ_MAPCHANGE, PTR("mac_05"), PTR("elv1"))
