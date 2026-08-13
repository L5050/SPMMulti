#pragma once
#include "common.h"

namespace mod {

typedef u32 (*CommandCb)(
    const u8* payload,
    size_t payloadLen,
    u8* response,
    size_t responseSize
);

enum CommandCategories : u8
{
    CMD_CAT_HELP = 0xFF, //special category for help requests, not an actual category of commands

    CMD_CAT_READ = 0x00,
    CMD_CAT_BASE = 0x01,
};

enum CommandId : u16 
{
    // 0xFFFF -- Help Command (not an actual command category, used for help requests)
    CMD_HELP = 0xFFFF,

    // 0x00xx -- Read Commands
    CMD_rIDX = 0x0000,
    CMD_rBUSY = 0x0001,

    // 0x01xx -- Base Commands
    CMD_ITEM = 0x0100,
    CMD_IDX = 0x0101,
};

class Command {

friend class CommandManager;
public:
    Command(
        CommandId id, 
        const char* name,
        const char* helpMsg,
        CommandCb cb
    );

    u32 executeBinary(
        const u8* payload,
        size_t payloadLen,
        u8* response,
        size_t responseSize
    ) const;
    const char* getName() const;
    const char* getHelpMsg() const;
private:
    CommandId id;
    const char* name; //unused outside debugging
    const char* helpMsg;
    CommandCb cb;
};

class CommandManager {
public:
    ~CommandManager();
    static CommandManager* CreateInstance();
    static CommandManager* Instance();
    static const int MAX_CATEGORY_ID = 0x10;
    static const int MAX_COMMAND_ID = 0x100;

    bool addCommand(const Command* cmd);
    const Command* findCommandById(CommandId id);
    
    u32 parseAndExecute(
        const u8* data,
        size_t len,
        u8* response,
        size_t responseSize
    );

private:
    CommandManager();
    static CommandManager* s_instance;
    static const Command* commandTable[MAX_CATEGORY_ID][MAX_COMMAND_ID];
};

extern "C" {
    void initCommands();
}

}