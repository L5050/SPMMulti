#include "commandmanager.h"
#include "common.h"

#include "commands.h"
#include "util.h"
#include <msl/stdio.h>
#include <msl/string.h>
#include <wii/os/OSError.h>

namespace mod {

//Define command categories modularly to keep CommandManager logic clean
void InitTestCommand(CommandManager* commandManager) {
    wii::os::OSReport("Adding multi command...\n");
    commandManager->addCommand(&multi);
}

void initCommands() {
    wii::os::OSReport("initCommands...\n");
    auto commandManager = CommandManager::CreateInstance();
    InitTestCommand(commandManager);
}

CommandManager* CommandManager::s_instance = nullptr;

Command::Command(CommandId id, const char* name, const char* helpMsg, CommandCb cb) {
    this->id = id;
    this->name = name;
    this->helpMsg = helpMsg;
    this->cb = cb;
}

u32 Command::executeBinary(
    const u8* payload,
    size_t payloadLen,
    u8* response,
    size_t responseSize
) const {
    // dolphin log for debugging
    wii::os::OSReport(
        "executeBinary: cmd=%s payloadLen=%zu\n",
        name,
        payloadLen
    );
    return cb(payload, payloadLen, response, responseSize);
}

/*const char* Command::getName() const {
    return name;
}*/

CommandManager::CommandManager() = default;
CommandManager::~CommandManager() = default;

CommandManager* CommandManager::CreateInstance() {
    if (s_instance == nullptr) {
        s_instance = new CommandManager;
    }
    return s_instance;
}

CommandManager* CommandManager::Instance() {
    return s_instance;
}

const Command* CommandManager::commandTable[1] = {};

const char* Command::getName() const {
    return name;
}

bool CommandManager::addCommand(const Command* cmd)
{
    u8 category = (cmd->id >> 8) & 0xFF;
    u8 command  = cmd->id & 0xFF;

    if (category != 0x19)
        {
            wii::os::OSReport("Bad category. Wanted 19, got %d\n", category);
            return false;
        }

    if (command != 0x99)
        {
            wii::os::OSReport("Bad command. Wanted 99, got %d\n", command);
            return false;
        }

    if (commandTable[0] != nullptr)
        {
            wii::os::OSReport("commandTable[0] not nullptr %d\n", commandTable[0]);
            return false;
        }

    commandTable[0] = cmd;
    wii::os::OSReport(
    "Register command %s (id=%04X cat=%u cmd=%u)\n",
    cmd->getName(),
    cmd->id,
    category,
    command
);
    return true;
}

u32 CommandManager::parseAndExecute(
    const u8* data,
    size_t len,
    u8* response,
    size_t responseSize
) {
    // must at least contain command_id + packet_length
    if (len < sizeof(u16) * 2) {
        wii::os::OSReport("Packet too small\n");
        return 0;
    }

    // parse header
    u16 fullId;
    u16 packetLength;

    msl::string::memcpy(&fullId, data, sizeof(u16));
    msl::string::memcpy(&packetLength, data + sizeof(u16), sizeof(u16));

    // sanity check
    if (packetLength != len) {
        wii::os::OSReport(
            "Packet length mismatch (hdr=%u, actual=%zu)\n",
            packetLength, len
        );
        return 0;
    }

    //special case for help command to live outside of 'normal' command table
    if (fullId == 1999)
    {
        const Command* pCmd = commandTable[0];
        if (!pCmd) {
            wii::os::OSReport("Unknown test command %04X\n", fullId);
            return 0;
        }
        // payload starts after the header
        const u8* payload = data + sizeof(u16) * 2;
        size_t payloadLen = len - sizeof(u16) * 2;

        return pCmd->executeBinary(payload, payloadLen, response, responseSize);
    }
    /*
    if (category >= MAX_CATEGORY_ID) {
        wii::os::OSReport("Invalid command category %u\n", category);
        return 0;
    }
    if (command >= MAX_COMMAND_ID) {
        wii::os::OSReport("Invalid command id %u\n", command);
        return 0;
    }*/

    //this branch should never be hit
    const Command* pCmd = commandTable[0];

    if (!pCmd) {
        wii::os::OSReport("Unknown command %04X\n", fullId);
        return 0;
    }

    // payload starts after the header
    const u8* payload = data + sizeof(u16) * 2;
    size_t payloadLen = len - sizeof(u16) * 2;

    return pCmd->executeBinary(payload, payloadLen, response, responseSize);
}


}
