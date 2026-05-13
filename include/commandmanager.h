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
    CMD_CAT_EFFECT = 0x02,
};

enum CommandId : u16 
{
    /* 
    NOTE: UNK FUNCTIONS BASED ON EU0 VERSION
    undefined4 seems to be a nintendo-specific bool-type(?)

    hitInit() starts at 8006aec8 -- inspect; compare to TTYD

    */

    CMD_MULTI = 0x1999,

    // 0xFFFF -- Help Command (not an actual command category, used for help requests)
    CMD_HELP = 0xFFFF,

    // 0x00xx -- Read Commands
    CMD_rIDX = 0x0000,
    CMD_rBUSY = 0x0001,

    // 0x01xx -- Base Commands
    CMD_ITEM = 0x0100,
    CMD_IDX = 0x0101,
    CMD_MAP_INTERACT = 0x0102,

    // 0x0200 - 0x0222 -- Effect_drv Commands                     | type: (args) |
    // 0x0230 - 0x0254 -- Effect_sub comands
    // 0x0260 - 0x02xx -- undetermined
    
    //effDrv
    CMD_EFF_INIT = 0x0200, //effInit                                void: (void)
    CMD_EFF_GETTEXOBJ = 0x0201, //effGetTexObj                      void: (uint param_1,GXTexObj *param_2)
    CMD_EFF_UNK_800615cc = 0x0202, //func_800615cc                  void: (undefined4 param_1,undefined4 *param_2,undefined4 *param_3)
    CMD_EFF_GETTEXOBJ64 = 0x0203, //effGetTexObj64                  void: (uint param_1,GXTexObj *param_2)
    CMD_EFF_AUTORELEASE = 0x0204, //effAutoRelease                  void: (int param_1)
    CMD_EFF_ENTRY = 0x0205, //effEntry                              EffEntry *: (void)
    CMD_EFF_ENTRYTYPE = 0x0206, //effEntryType                      EffEntry *: (int type)
    CMD_EFF_SETNAME = 0x0207, //effSetName                          void: (EffEntry *param_1,char *param_2)
    CMD_EFF_MAIN = 0x0208, //effMain                                void: (void)
    CMD_EFF_UNK_80061a34 = 0x0209, //func_80061a34                  void: (void)
    CMD_EFF_SYSFILESLOADED = 0x020A, //effSystemFilesLoaded         bool: (void)
    CMD_EFF_DELETE = 0x020B, //effDelete                            void: (EffEntry *param_1)
    CMD_EFF_SOFTDELETE = 0x020C, //effSoftDelete                    void: (EffEntry *param_1)
    CMD_EFF_NAMETOPTR = 0x020D, //effNameToPtr                      EffEntry *: (char *instanceName)
    CMD_EFF_GETSET = 0x020E, //effGetSet                            EffSet *: (char *effectName)
    CMD_EFF_FIRECOUNTENTRIES = 0x020F, //effFireCountEntries        int: (double distanceLimit,Vec3 *pos)
    CMD_EFF_UNK_80061d78 = 0x0210, //func_80061d78                  int: (void)
    CMD_EFF_UNK_80061e18 = 0x0211, //func_80061e18                  void: (char *param_1,int param_2,int *param_3)
    CMD_EFF_UNK_80061eb8 = 0x0212, //func_80061eb8                  undefined4: (double param_1,double param_2,EffEntry *param_3,GravityType param_4,float *param_5,float *param_6,float *param_7,float *param_8,float *param_9_00,float *param_10_00,float *param_11,float *param_12,undefined4 *param_13)
    CMD_EFF_UNK_800623f8 = 0x0213, //func_800623f8                  undefined4: (double param_1,double param_2,double param_3,double param_4,double param_5,double param_6,int *param_7,int param_8,Mtx34 *param_9,float *param_10,float *param_11))
    CMD_EFF_UNK_80062d04 = 0x0214, //func_80062d04                  undefined4: (int *param_1,Mtx32 *param_2)
    CMD_EFF_UNK_80062f9c = 0x0215, //func_80062f9c                  undefined4: (int *param_1)
    CMD_EFF_UNK_800630b8 = 0x0216, //func_800630b8                  undefined4: (int *param_1,undefined4 param_2,undefined4 param_3,undefined4 param_4)
    CMD_EFF_UNK_80063130 = 0x0217, //func_80063130                  void: (int *param_1,int param_2)
    CMD_EFF_UNK_80063198 = 0x0218, //func_80063198                  void: (EffEntry *eff)
    CMD_EFF_UNK_800631a4 = 0x0219, //func_800631a4                  undefined4: (undefined4 *param_1)

    /*actor-aware helpers, likely follow structure of below funcs where they're for type 1 and 2 respectively to pass the higher-level func the required pos*/
    CMD_EFF_UNK_800631ac = 0x021A, //func_800631ac                  void: (void *eff)
    CMD_EFF_UNK_800631b8 = 0x021B, //func_800631b8                  void: (void *param_1,int evtId,char *npcName)

        //non-namespaced funcs, still likely related to effdrv as more effdrv funcs follow. These seem to be helpers to get the pos for where an eff should spawn in real-time
    /* Seems that it specifically targets if type=3(below) to convert the requested type to pass forward the explicit XYZ params */
    CMD_EFF_UNK_80063290 = 0x021C, //func_80063290                  void:(double param_1,double param_2,double param_3,double param_4,double param_5,double param_6,undefined4 *param_7)

    /* Seems that it's a helper for type=4, presumably to set the ID of the Pixl for the higher-level func grabbing it's position */
    CMD_EFF_UNK_800632ec = 0x021D, //func_800632ec                  void: (undefined4 *param_1,undefined4 param_2)

    /* Seems that it's a helper for type=5, Tippi, for the higher-level func grabbing it's position. Unnecessary, but likely to follow the helper format they'd designed */
    CMD_EFF_UNK_800632fc = 0x021E, //func_800632fc                  void: (undefined4 *param_1)

        //back to namespaced
    /* Seems to be a helper to get GravityType based on type val*/
    CMD_EFF_UNK_80063308 = 0x021F, //func_80063308                  int: (int *param_1)
    CMD_EFF_SETOFFSCREEN = 0x0220, //effSetOffscreen                void: (char *param_1)
    CMD_EFF_DELETEOFFSCREEN = 0x0221, //effDeleteOffscreen          void: (char *param_1)
    CMD_EFF_UNK_8006ccfc = 0x0222, //func_800633fc                  int: (void)

    //Gonna pad to the next 0x010 for the next category to leave room for any additional effDrv funcs we might find
    /* 
    Notably, the branch in FUN_80061eb8 that checks for a specific effect entry type (type 0x1D)
        this seems to be related to the character targeted by the branch, where
        type 1 = Mario
        type 2 = NPC
        type 3 = Manual XYZ coords
        type 4 = Fairy (Pixl(?))
        type 5 = Guide (Tippi(?))
    */

    //eff_sub
    CMD_EFF_S_UNK_8005b534 = 0x0230, //func_8005b534                void: (Mtx32 *param_1,Vec3 *param_2)
    CMD_EFF_S_UNK_8005b60c = 0x0231, //func_8005b60c                void: (undefined4 *param_1,int param_2,undefined4 param_3,undefined4 param_4,undefined4 param_5)
    CMD_EFF_S_INIT = 0x0232, //effSubInit                           void: (EffSubWork *wp)
    CMD_EFF_S_DELETE = 0x0233, //effSubDelete                       void: (void)
    CMD_EFF_S_MAIN = 0x0234, //effSubMain                           void: (void)
    CMD_EFF_S_EFFDATALOADED = 0x0235, //effSubEffdataLoaded         bool: (void)
    /* TODO: Look into; high-level eff file loading/binding */
    CMD_EFF_S_LOAD = 0x0236, //effSubLoad                           void: (int fileId)
    /* BIG TODO; VERY LOW-LEVEL EMITTER RENDERER -- PURE HEART GEOMETRY EMBEDDED? */
    CMD_EFF_S_DISPGX = 0x0237, //effSubDispGX                       void: (double time,EffdataHeader *data,int param_3,Mtx34 *param_4,byte param_5,int param_6)
    /* BIG TODO; VERY LOW-LEVEL EMITTER INTERPRETER */
    CMD_EFF_S_DISPSUB = 0x0238, //effSubDispSub                     void: (EffdataHeader *data,int unk28,int id,Mtx34 *mtx,double time,int param_6,uint param_7)
    CMD_EFF_S_DISP = 0x0239, //effSubDisp                           void: (uint handle,Mtx34 *mtx,undefined8 time)
    CMD_EFF_S_UNK_8005f9dc = 0x023A, //func_8005f9dc                void: (unefined4 param_1)
    CMD_EFF_S_UNK_8005f9e8 = 0x023B, //func_8005f9e8                void: (double param_1,undefined4 param_2,undefined1 *param_3,undefined1 *param_4)
    CMD_EFF_S_UNK_8005fa44 = 0x023C, //func_8005fa44                void: (double param_1,double param_2,double param_3,undefined4 param_4)
    CMD_EFF_S_MAPWATERSETDISPBOUNDARIES = 0x023D, //effSubMapWaterSetDispBoundaries void: (double dispBoundaryOriginX,double dispBoundaryOriginZ,double dispBoundaryWidthX,double dispBoundaryWidthZ,int param_5)
    CMD_EFF_S_UNK_8005fa9c = 0x023E, //func_8005fa9c                void: (void *param,undefined4 param_2)
    
        //Color override helpers, presumably to set effSubWp 'scratch' fields to runtime-memory addresses for real-time color overrides in the next func, which has an int 'enable' func, presumably for a mode type rather than bool
    /* NOTE: EffSubWp field82_0x88 = RGBA (acc. by `(eff->field)(.red | .green | .blue | .alpha))` -- col0 */
    CMD_EFF_S_UNK_8005fab0 = 0x023F, //func_8005fab0                void: (RGBA *param_1)
    /* NOTE: EffSubWp field83_0x8c = RGBA (acc. by `(eff->field)(.red | .green | .blue | .alpha))`) -- col1 */
    CMD_EFF_S_UNK_8005fad8 = 0x0240, //func_8005fad8                void: (RGBA *param_1)
    /* NOTE: Color override func, presumably param_2 = col0 / param_3 = col1 */
    CMD_EFF_S_SETCOLOROVERRIDE = 0x0241, //effSubSetColorOverride   void: (int enable,RGBA *param_2,RGBA *param_3)
    CMD_EFF_S_SETCULLMODE = 0x0242, //effSubSetCullMode             void: (int param_1)
    CMD_EFF_S_UNK_8005fb68 = 0x0243, //func_8005fb68                void: (undefined4 param_1)
    CMD_EFF_S_UNK_8005fb74 = 0x0244, //func_8005fb74                void: (undefined4 param_1)
    CMD_EFF_S_UNK_8005fb80 = 0x0245, //func_8005fb80                void: (uint param_1,undefined4 param_2)
    CMD_EFF_S_UNK_8005fbe4 = 0x0246, //func_8005fbe4                void: (int param_1)
    CMD_EFF_S_UNK_8005fc3c = 0x0247, //func_8005fc3c                void: (uint param_1)
    CMD_EFF_S_UNK_8005fc48 = 0x0248, //func_8005fc48                void: (double param_1,double param_2,undefined1 param_3)
    /* (effSubDataSearch)
    *******************************************************************
    * Return value:                                                   *
    * * 0x8000_0000: load from effect file instead of general effdata *
    * * 0x7fff_0000: section id                                       *
    * * 0x0000_ffff: entry number                                     *
    *******************************************************************
    */
   CMD_EFF_S_DATSEARCH = 0x0249, //effSubDatSearch                  int: (char *sectionNames,char *entryName)
   CMD_EFF_S_DATGETNUMFRAMES = 0x024A, //effSubDatGetNumFrames      ushort: (uint handle)
   CMD_EFF_S_UNK_8005ff78 = 0x024B, //func_8005ff78                 uint: (int param_1,int param_2,int param_3)
   CMD_EFF_S_UNK_8006002c = 0x024C, //func_8006002c                 int: (int param_1,int param_2,int param_3,undefined4 param_4)
   CMD_EFF_S_UNK_800603b0 = 0x024D, //func_800603b0                 uint: (uint handle,undefined4 param_2)
   CMD_EFF_S_UNK_80060bc8 = 0x024E, //func_80060bc8                 undefined4 *: (void)
   CMD_EFF_S_ENTRY = 0x024F, //effSubEntry                          undefined4: (char* name)
   CMD_EFF_S_SETWATEREFFCOLORS = 0x0250, //effSubSetWaterEffColors  void: (double y,RGBA *col1,RGBA *col2)
   CMD_EFF_S_UNK_800611cc = 0x0251, //func_800611cc                 byte: (void)
   CMD_EFF_S_UNK_800611d8 = 0x0252, //func_800611d8                 void: (byte param_1)
   CMD_EFF_S_UNK_800611e4 = 0x0253, //func_800611e4                 undefined4: (int param_1)
   CMD_EFF_S_UNK_MAPWATERCREATEDISPBOUNDARIES = 0x0254, //func_80061254 undefined4: (double posX,float *limitXOrigin,float *limitZOrigin,float *limitXWidth,float *limitZWidth)

   //Here's the start of effdrv funcs; they're flipped in order compared to ghidra, but the effdrv seem more useful for high-level user use-case 

    
    //effEntry commands (span from 80091654 to 800d1c80) (eu0)
    /*
    Seems most follow a pattern of 
        func1: Entry
        func2: Main
        func3: Display

        where some may have a few between func2 and func3 for helpers, presumably;
            i.e. probably like cloud objects having a helper for interaction events, to work like a mix of eff/MOBJs
        todo: learn about spawn order
    */

    CMD_EFF_E_SPMRECOVERY = 0x0260, //effSpmRecoveryEntry           EffEntry *: (double x,double y,double z,int hp)
    CMD_EFF_E_UNK_80091788 = 0x0261, //func_80091788                void: (EffEntry *param_1)
    CMD_EFF_E_UNK_80091794 = 0x0262, //func_80091794                void: (EffEntry *param_1)
    CMD_EFF_E_SPMRECOVERYSUBA = 0x0263, //effSpmRecoverySubAEntry   EffEntry *: (float param_1,uint param_2)
    CMD_EFF_E_SPMRECOVERYSUBB = 0x0264, //effSpmRecoverySubBEntry   EffEntry *: (float param_1,int param_2)
    CMD_EFF_E_SPMRECOVERYSUBD = 0x0265, //effSpmRecoverySubDEntry   EffEntry *: (double param_1,undefined4 param_2)
    CMD_EFF_E_SPMRECOVERYMAIN = 0x0266, //effSpmRecoveryMain        void: (EffEntry *param_1)

    CMD_EFF_E_VOLTENTRY = 0x0270, //effSpmVoltEntry              EffEntry *: (double x,double y,int *param_3,int param_4)

    CMD_EFF_E_ITEMTHUNDER = 0x0280, //effItemThunderEntry        EffEntry *: (float x, float y, float z, s32 param_4, s32 param_5, s32 param_6, s32 param_7, s32 param_8)
    /*
    CMD_EFFECT_DAMAGESTAR = 0x0215, //effDamageStarEntry            EffEntry *: (float x, float y, float z,int damageType, s32 damage)
    */
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
    //static const int MAX_CATEGORY_ID = 0x10;
    //static const int MAX_COMMAND_ID = 0x100;

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
    static const Command* commandTable[1];
};

extern "C" {
    void initCommands();
}

}