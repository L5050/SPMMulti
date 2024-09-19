#include "mod.h"
#include "commandmanager.h"
#include "patch.h"
#include "netmemoryaccess.h"
#include "network.h"
#include "spmhttp.h"
#include "chainloader.h"
#include "cutscene_helpers.h"
#include "evt_cmd.h"
#include "evtpatch.h"
#include "exception.h"
#include "evtdebug.h"
#include "romfontexpand.h"

#include <spm/evt_seq.h>
#include <spm/evtmgr.h>
#include <spm/map_data.h>
#include <spm/fontmgr.h>
#include <spm/seqdrv.h>
#include <spm/seqdef.h>
#include <spm/spmario.h>
#include <wii/os/OSError.h>
#include <wii/ipc.h>
#include <wii/os.h>
#include <msl/stdio.h>

namespace mod {
bool gIsDolphin;
bool tfirstRun = false;
/*
    Title Screen Custom Text
    Prints "SPM Rel Loader" at the top of the title screen
*/

static spm::seqdef::SeqFunc *seq_titleMainReal;
static void seq_titleMainOverride(spm::seqdrv::SeqWork *wp)
{
    wii::gx::GXColor _colour {0, 255, 0, 255};
    f32 scale = 0.8f;
    char msg[128];
    u32 ip = Mynet_gethostip();
    msl::stdio::snprintf(msg, 128, "%d.%d.%d.%d\n", ip >> 24 & 0xff, ip >> 16 & 0xff, ip >> 8 & 0xff, ip & 0xff);
    spm::fontmgr::FontDrawStart();
    spm::fontmgr::FontDrawEdge();
    spm::fontmgr::FontDrawColor(&_colour);
    spm::fontmgr::FontDrawScale(scale);
    spm::fontmgr::FontDrawNoiseOff();
    spm::fontmgr::FontDrawRainbowColorOff();
    f32 x = -((spm::fontmgr::FontGetMessageWidth(msg) * scale) / 2);
    spm::fontmgr::FontDrawString(x, 200.0f, msg);
    seq_titleMainReal(wp);
}
static void titleScreenCustomTextPatch()
{
    seq_titleMainReal = spm::seqdef::seq_data[spm::seqdrv::SEQ_TITLE].main;
    spm::seqdef::seq_data[spm::seqdrv::SEQ_TITLE].main = &seq_titleMainOverride;
}

static void checkForDolphin()
{
    // Thanks to TheLordScruffy for telling me about this
    gIsDolphin = wii::ipc::IOS_Open("/sys", 1) == -106;

    // If they ever fix that, it'll be in a version that's definitely new enough to have /dev/dolphin
    if (!gIsDolphin)
    {
        int ret = wii::ipc::IOS_Open("/dev/dolphin", 0);
        if (ret >= 0)
        {
            gIsDolphin = true;
            wii::ipc::IOS_Close(ret);
        }
    }
}

/*
    General mod functions
*/

spm::evtmgr::EvtScriptCode* getInstructionEvtArg(spm::evtmgr::EvtScriptCode* script, s32 line, int instruction)
{
  spm::evtmgr::EvtScriptCode* link = evtpatch::getEvtInstruction(script, line);
  wii::os::OSReport("%x\n", link);
  spm::evtmgr::EvtScriptCode* arg = evtpatch::getInstructionArgv(link)[instruction];
  wii::os::OSReport("%x\n", arg);
  return arg;
}

void webhookShenanigans()
{
  wii::os::OSReport("SPM Rel Loader: the mod has ran!\n");
  HTTPResponse_t* myHttpResponse;
  HTTPStatus_t mystatus = HTTPGet("34.173.153.191", 80, "/", myHttpResponse);
  wii::os::OSReport("%d\n", mystatus);
}

s32 startWebhook(spm::evtmgr::EvtEntry * evtEntry, bool firstRun) {
  webhookShenanigans();
  return 2;
}

EVT_DECLARE_USER_FUNC(startWebhook, 0)

EVT_BEGIN(connectToServer)
//USER_FUNC(spm::evt_seq::evt_seq_set_seq, spm::seqdrv::SEQ_MAPCHANGE, PTR("mac_05"), PTR("elv1"))
  USER_FUNC(startWebhook)
RETURN_FROM_CALL()

void patchScripts()
{
  spm::map_data::MapData * an1_01_md = spm::map_data::mapDataPtr("aa1_01");
  spm::evtmgr::EvtScriptCode* transition_evt = getInstructionEvtArg(an1_01_md->initScript, 60, 0);
  evtpatch::hookEvt(transition_evt, 10, (spm::evtmgr::EvtScriptCode*)connectToServer);
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
    //webhookShenanigans();
    titleScreenCustomTextPatch();
    patchScripts();
    //tryChainload();
}

}
