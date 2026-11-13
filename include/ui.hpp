#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "app_update.hpp"
#include "guardian_core.h"
#include "guardian_addons.h"
namespace nmguardian {
inline constexpr char AppVersion[]="0.1.1";
inline constexpr char AppPath[]="sdmc:/switch/NinMagicGuardian/NinMagic-Guardian.nro";
enum class Screen {Home,Options,BackupConfirm,RestoreConfirm,RescueConfirm,AddonReview,QuarantineConfirm,ReactivateConfirm,Help,Update,ConfirmUpdate,ConfirmArchive,Busy,Result};
struct View {
    Screen screen=Screen::Home;
    size_t selected=0;
    std::string firmware,model,environmentDetail,message,operation,progressPath,updateError,recoveryError;
    bool atmosphere=false,identityValid=false,appletMode=false,success=false,queryComplete=false,restartRequired=false;
    bool sourceReady=false;
    unsigned options=0;
    gc_backup_info backup{},source{};
    ga_review addons{};
    std::vector<bool> markedAddons;
    uint64_t current=0,total=0;
    double seconds=0;
    nm::AppUpdateInfo update;
    nm::CompletedUpdateInfo updateRecord;
};
bool uiInit(const std::string&);
void uiDraw(uint32_t*,int,const View&);
void uiExit();
std::string formatBytes(uint64_t);
inline bool publishedAppOnSd(const nm::AppUpdateInfo&u){return u.sdVerified&&!u.version.empty()&&u.sdVersion==u.version&&u.sdSha256==u.sha256&&u.sdSize==u.size;}
inline bool canDownloadAppUpdate(const View&v){return v.queryComplete&&v.updateError.empty()&&v.update.configured&&v.update.available&&!v.update.installed&&!publishedAppOnSd(v.update)&&v.updateRecord.state==nm::CompletedUpdateState::None&&v.recoveryError.empty();}
}
