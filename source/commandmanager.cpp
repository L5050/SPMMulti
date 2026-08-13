#include "commandmanager.h"
#include "common.h"

#include "commands.h"
#include "util.h"
#include <msl/stdio.h>
#include <msl/string.h>
#include <wii/os/OSError.h>

namespace mod {

void initCommands() {
    auto commandManager = CommandManager::CreateInstance();
    
    commandManager->addCommand(&ap);
}

CommandManager* CommandManager::s_instance = nullptr;

Command::Command(const char* name, const char* helpMsg, CommandCb cb) {
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

const char* Command::getName() const {
    return name;
}
const char* Command::getHelpMsg() const {
    return helpMsg;
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

const Command* CommandManager::commandPointer = nullptr;



bool CommandManager::addCommand(const Command* cmd)
{
    commandPointer = cmd;
    wii::os::OSReport(
        "Register command %s\n",
        cmd->getName()
    );
    
    return true;
}


u32 CommandManager::parseAndExecute(
    const u8* data,
    size_t len,
    u8* response,
    size_t responseSize
)   {
        // sanity check
        if (len != 8) {
            wii::os::OSReport("Packet size mismatch\n");
            return 0;
        }

        const Command* pCmd = commandPointer;
        if (pCmd == nullptr)
        {
            wii::os::OSReport("No command pointer at [1][1]\n");
            return 0;
        }

        return pCmd->executeBinary(data, 8, response, responseSize);
    }
}
