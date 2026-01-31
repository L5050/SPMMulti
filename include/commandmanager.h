#pragma once
#include "common.h"

#include <EASTL/map.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
using eastl::string;

namespace mod {

typedef u32 (*CommandCb)(eastl::vector<const char*> &args, u8* response, size_t responseSize);

enum CommandId {
        CMD_ITEM = 1,
    };

class Command {
friend class CommandManager;
public:
    Command(
        CommandId id, 
        const char* name, //unused outside debugging
        const char* helpMsg, 
        s32 argc, 
        CommandCb cb
    );
    //u32 execute(eastl::vector<const char*> &args, u8* response, size_t responseSize) const;
    u32 executeBinary(
        const u8* payload,
        size_t payloadLen,
        u8* response,
        size_t responseSize
    ) const;
    const char* getName() const;
    const char* getHelpMsg() const;
    s32 getArgc() const;
private:
    CommandId id;
    const char* name; //unused outside debugging
    const char* helpMsg;
    s32 argc;
    CommandCb cb;
};

class CommandManager {
public:
    ~CommandManager();
    static CommandManager* CreateInstance();
    static CommandManager* Instance();
    const Command* findCommand(const char* name);
    const Command* findCommandById(CommandId id);
    bool addCommand(const Command* cmd);
    bool removeCommand(const char* name);
    u32 parseAndExecute(
        const u8* data,
        size_t len,
        u8* response,
        size_t responseSize
    );

    eastl::vector<const Command*> commands;
private:
    CommandManager();
    static CommandManager* s_instance;
};

extern "C" {
    void initCommands();
}

}