#pragma once
#include "common.h"

namespace mod {

typedef u32 (*CommandCb)(
    const u8* payload,
    size_t payloadLen,
    u8* response,
    size_t responseSize
);

class Command {

friend class CommandManager;
public:
    Command(
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
    
    u32 parseAndExecute(
        const u8* data,
        size_t len,
        u8* response,
        size_t responseSize
    );

private:
    CommandManager();
    static CommandManager* s_instance;
    static const Command* commandPointer;
};

extern "C" {
    void initCommands();
}

}