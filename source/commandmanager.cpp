#include "commandmanager.h"
#include "common.h"

#include "commands.h"
#include "util.h"
#include <msl/stdio.h>
#include <msl/string.h>
#define EASTL_USER_CONFIG_HEADER <eastl_config.h>
#include <EASTL/algorithm.h>
#include <EASTL/map.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <wii/os/OSError.h>
using eastl::string;

namespace mod {

void initCommands() {
    auto commandManager = CommandManager::CreateInstance();
    /*commandManager->addCommand(&read);
    commandManager->addCommand(&write);
    commandManager->addCommand(&msgbox);*/
    commandManager->addCommand(&item);
}

CommandManager* CommandManager::s_instance = nullptr;

Command::Command(CommandId id, const char* name, const char* helpMsg, s32 argc, CommandCb cb) {
    this->id = id;
    this->name = name;
    this->helpMsg = helpMsg;
    this->argc = argc;
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
    return 0;
}

/*u32 Command::execute(eastl::vector<const char*> &args, u8* response, size_t responseSize) const {
    s32 realArgc = args.size();
    if (realArgc != argc && argc != ANY_ARGC) {
        msl::stdio::snprintf((char*)response, responseSize, "Error: expected %d arguments, got %d\n", argc, realArgc);
        return msl::string::strlen((char*)response);
    }
    return cb(args, response, responseSize);
}*/

const char* Command::getName() const {
    return name;
}
const char* Command::getHelpMsg() const {
    return helpMsg;
}
s32 Command::getArgc() const {
    return argc;
}


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

const Command* CommandManager::findCommand(const char* name) {
    auto cmd = eastl::find_if(commands.begin(), commands.end(), [name](const Command* cmd) { return msl::string::strcmp(cmd->getName(), name) == 0; });
    if (cmd != eastl::end(commands)) {
        return *cmd;
    }
    return nullptr;
}

bool CommandManager::addCommand(const Command* cmd) {
    if (findCommand(cmd->name) != nullptr)
        return false;
    commands.push_back(cmd);
    return true;
}

bool CommandManager::removeCommand(const char* name) {
    auto cmd = findCommand(name);
    if (cmd != nullptr) {
        commands.erase(&cmd);
        return true;
    }
    return false;
}

const Command* CommandManager::findCommandById(CommandId id) {
    for (auto* cmd : this->commands) {
        if (cmd->id == id)
            return cmd;
    }
    return nullptr;
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
    u16 commandId;
    u16 packetLength;

    msl::string::memcpy(&commandId, data, sizeof(u16));
    msl::string::memcpy(&packetLength, data + sizeof(u16), sizeof(u16));

    // sanity check
    if (packetLength != len) {
        wii::os::OSReport(
            "Packet length mismatch (hdr=%u, actual=%zu)\n",
            packetLength, len
        );
        return 0;
    }

    const Command* pCmd = findCommandById((CommandId)commandId);
    if (!pCmd) {
        wii::os::OSReport("Unknown command id %u\n", commandId);
        return 0;
    }

    // payload starts after the header
const u8* payload = data + sizeof(u16) * 2;
size_t payloadLen = len - sizeof(u16) * 2;

// only have item command set up, but set up for future expansion
switch ((CommandId)commandId) {
case CMD_ITEM: {
    return handleItemBinary(payload, payloadLen, response, responseSize);
}
default:
    wii::os::OSReport("Unknown command id %u\n", commandId);
    return 0;
}
}


}