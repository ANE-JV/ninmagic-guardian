#include <switch.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdio>
#include <ctime>
#include <cstring>
#include "ui.hpp"
#include "guardian_native.hpp"
#include "rescue_install.hpp"
#include "guardian_sha256.h"
#include "guardian_recovery.h"
#include "update_network.hpp"
namespace {
using namespace nmguardian;
enum class Kind {Init,Scan,Create,Verify,ReviewRestore,Restore,Rescue,Options,InspectAddons,Quarantine,Reactivate,DiscardExtras,CheckUpdate,PrepareUpdate,InstallUpdate,Archive};
NativeFs sd{"sdmc:"};
struct Job {
    Kind kind=Kind::Init;Thread thread{};bool active=false,ok=false;
    std::atomic<bool>done{false};std::atomic<uint64_t>current{0},total{0};
    Mutex progressMutex{};char path[512]{};
    Runtime runtime;gc_context context{};gc_result result{};unsigned options=0;
    nm::AppUpdateInfo update;nm::CompletedUpdateInfo record;
    std::string error,message,recoveryError;
    std::array<unsigned char,32> reviewDigest{};
    bool reviewReady=false,restoreWithAddons=false,restorePermanent=false;
    ga_review addons{};
}job;
bool romfsMounted=false;
void progress(void*,const char*p,uint64_t current,uint64_t total){mutexLock(&job.progressMutex);std::snprintf(job.path,sizeof(job.path),"%s",p?p:"");mutexUnlock(&job.progressMutex);job.current.store(current,std::memory_order_relaxed);job.total.store(total,std::memory_order_relaxed);}
void makeContext(){
    job.runtime=inspectRuntime();job.context={};job.context.fs=sd.callbacks();
    std::snprintf(job.context.identity,sizeof(job.context.identity),"%s",job.runtime.identity.c_str());
    std::snprintf(job.context.firmware,sizeof(job.context.firmware),"%s",job.runtime.firmware.c_str());
    std::snprintf(job.context.model,sizeof(job.context.model),"%s",job.runtime.model.c_str());
    std::time_t now=std::time(nullptr);std::tm t{};gmtime_r(&now,&t);std::strftime(job.context.created,sizeof(job.context.created),"%Y-%m-%d %H:%M UTC",&t);
    job.context.progress=progress;
}
bool canWrite(){if(!job.runtime.atmosphere){job.error="No se pudo confirmar Atmosphère. Reabre Guardian desde hbmenu en el entorno correcto.";return false;}if(!job.runtime.identityValid){job.error="No se pudo identificar la microSD. No se escribirán el respaldo ni los archivos del arranque. "+job.runtime.detail;return false;}return true;}
bool sameCard(const gc_backup_info&b){std::string id=b.identity;for(char&c:id)if(c>='A'&&c<='F')c=char(c-'A'+'a');return job.runtime.identityValid&&id==job.runtime.identity;}
bool currentDigest(std::array<unsigned char,32>&out){
    auto f=sd.callbacks();const char*p=GC_BACKUP_ROOT "/current/manifest.txt";gc_stat s{};
    if(f.stat(f.user,p,&s)!=1||s.type!=GC_FILE||s.size>2*1024*1024)return false;
    auto*h=f.open(f.user,p,'r');if(!h)return false;gc_sha256 hash;gc_sha256_init(&hash);unsigned char buffer[4096];uint64_t count=0;bool ok=true;
    for(;;){size_t n=0;if(f.read(f.user,h,buffer,sizeof(buffer),&n)){ok=false;break;}if(!n)break;count+=n;if(count>s.size){ok=false;break;}gc_sha256_update(&hash,buffer,n);}
    if(f.close(f.user,h)||count!=s.size)ok=false;
    if(ok)gc_sha256_final(&hash,out.data());
    return ok;
}
void useResult(gc_result r){job.result=r;job.ok=r.ok;job.message=r.message;if(!r.ok)job.error=r.message;}
bool inspectAddons(){
    ga_review pending{};
    if(ga_pending_review(&job.context,&pending)){job.error="No se pudo comprobar el plan de extras: "+std::string(pending.message);job.ok=false;return false;}
    job.addons=(pending.quarantine_pending||pending.discard_pending)?pending:ga_inspect(&job.context);
    return true;
}
bool samePlan(const ga_review&a,const ga_review&b){
    if(!a.ok||!b.ok||a.count!=b.count||std::strcmp(a.backup_sha256,b.backup_sha256))return false;
    for(unsigned i=0;i<a.count;++i)if(a.candidates[i].kind!=b.candidates[i].kind||std::strcmp(a.candidates[i].title_id,b.candidates[i].title_id)||std::memcmp(a.candidates[i].fingerprint,b.candidates[i].fingerprint,32))return false;
    return true;
}
void work(void*){
    job.error.clear();job.message.clear();job.ok=false;job.result={};makeContext();
    switch(job.kind){
        case Kind::Init:job.options=loadOptions(sd.callbacks());job.result=gc_inspect(&job.context,0);job.ok=true;break;
        case Kind::Scan:useResult(gc_scan_source(&job.context,job.options));break;
        case Kind::Create:if(canWrite()){useResult(gc_create_backup(&job.context,job.options));if(job.ok){auto inventory=ga_capture(&job.context);if(!inventory.ok){job.ok=false;job.error="El respaldo del arranque quedó verificado, pero no se pudo guardar el inventario de complementos. "+std::string(inventory.message);}else job.message+=" Inventario de temas y módulos guardado sin copiar sus archivos.";}}break;
        case Kind::Verify:useResult(gc_inspect(&job.context,1));if(job.ok&&!job.result.info.present){job.ok=false;job.error="Todavía no hay un respaldo que comprobar. Créalo cuando todo funcione bien.";}else if(job.ok&&!sameCard(job.result.info)){job.ok=false;job.error="Se pudieron leer las huellas, pero esta copia no se ha confirmado para la microSD actual. No se utilizará para restaurar.";}break;
        case Kind::ReviewRestore:{
            job.reviewReady=false;if(!canWrite())break;
            auto recovered=gc_recover_backup(&job.context);if(!recovered.ok){useResult(recovered);break;}
            useResult(gc_inspect(&job.context,1));if(!job.ok)break;if(!job.result.info.present){job.ok=false;job.error="No hay un respaldo completo para restaurar.";break;}
            if(!sameCard(job.result.info)){job.ok=false;job.error="Este respaldo pertenece a otra microSD. Se conserva sin restaurarlo.";break;}
            job.reviewReady=currentDigest(job.reviewDigest);if(!job.reviewReady){job.ok=false;job.error="No se pudo fijar la copia que vas a revisar.";}
            if(job.reviewReady)inspectAddons();
                    break;
        }
        case Kind::Restore:{
            if(!canWrite())break;
            auto recovered=gc_recover_backup(&job.context);if(!recovered.ok){useResult(recovered);break;}
            auto inspect=gc_inspect(&job.context,1);if(!inspect.ok){useResult(inspect);break;}
            std::array<unsigned char,32>actual{};if(!job.reviewReady||!currentDigest(actual)||actual!=job.reviewDigest){job.error="El respaldo cambió desde la revisión. Vuelve a Restaurar para revisar y confirmar la copia actual.";job.reviewReady=false;break;}
            if(inspect.info.firmware[0]&&job.runtime.firmware!=std::string(inspect.info.firmware)){job.error="El firmware actual ("+job.runtime.firmware+") es diferente al registrado en el respaldo ("+std::string(inspect.info.firmware)+"). Esta beta no restaurará archivos de otra versión desde Horizon. Usa archivos compatibles con el firmware actual; el respaldo no lo revierte.";break;}
            if(job.restorePermanent&&!prepareRescue(sd.callbacks(),"romfs:",job.error)){job.error="No se inició la eliminación: primero debe quedar verificado el rescatador compatible. "+job.error;break;}
            auto restored=gr_restore_mode(&job.context,job.restoreWithAddons?&job.addons:nullptr,job.restorePermanent?1:0);
            useResult(restored.result);break;
        }
        case Kind::Rescue:if(canWrite()){job.ok=prepareRescue(sd.callbacks(),"romfs:",job.message);if(!job.ok)job.error=job.message;}
                    break;
        case Kind::Options:if(canWrite())job.ok=saveOptions(sd.callbacks(),job.options,job.error);break;
        case Kind::InspectAddons:job.ok=inspectAddons();break;
        case Kind::Quarantine:if(canWrite()){auto r=ga_quarantine(&job.context,&job.addons);job.ok=r.ok;job.message=r.message;if(!r.ok)job.error=r.message;}break;
        case Kind::Reactivate:if(canWrite()){auto r=ga_restore_quarantine(&job.context);job.ok=r.ok;job.message=r.message;if(!r.ok)job.error=r.message;}break;
        case Kind::DiscardExtras:if(canWrite()){
            ga_review pending{};
            if(ga_pending_review(&job.context,&pending)||!samePlan(job.addons,pending)||!(pending.quarantine_pending||pending.discard_pending)){job.error="El plan de extras cambió desde la revisión. No se eliminó nada: vuelve a revisar la lista.";break;}
            if(!prepareRescue(sd.callbacks(),"romfs:",job.error)){job.error="No se inició la eliminación: no se pudo verificar el rescatador compatible. "+job.error;break;}
            auto r=ga_discard_quarantine(&job.context);job.ok=r.ok;job.message=r.message;if(!r.ok)job.error=r.message;
        }break;
        case Kind::CheckUpdate:job.ok=nm::checkAppUpdate(job.update,job.error);{std::string ignored;nm::canArchiveCompletedUpdate(AppPath,job.record,ignored);}
                    break;
        case Kind::PrepareUpdate:job.ok=nm::prepareAppUpdate(job.update,AppPath,job.error);break;
        case Kind::InstallUpdate:job.ok=nm::installPreparedAppUpdate(job.update,AppPath,job.error);{std::string ignored;nm::canArchiveCompletedUpdate(AppPath,job.record,ignored);}job.message=job.ok?job.update.detail:job.error;break;
        case Kind::Archive:job.ok=nm::archiveCompletedUpdate(AppPath,job.record,job.error);job.message="Registro finalizado archivado. Se conservan el NRO y sus copias anteriores.";break;
    }
    job.done.store(true,std::memory_order_release);
}
bool start(Kind kind,View&v,const std::string&label){
    if(job.active)return false;
    job.kind=kind;job.update=v.update;job.record=v.updateRecord;job.options=v.options;job.recoveryError=v.recoveryError;job.current.store(0);job.total.store(0);job.path[0]=0;job.done.store(false);
    if(kind==Kind::Quarantine){job.addons=v.addons;if(!v.addons.quarantine_pending){job.addons.count=0;for(unsigned i=0;i<v.addons.count&&i<v.markedAddons.size();++i)if(v.markedAddons[i])job.addons.candidates[job.addons.count++]=v.addons.candidates[i];}}
    if(kind==Kind::Restore){job.addons=v.addons;job.restoreWithAddons=v.restoreAddons&&v.addons.ok&&(v.addons.count||v.addons.quarantine_pending||v.addons.discard_pending);if(job.restoreWithAddons&&!v.addons.quarantine_pending&&!v.addons.discard_pending){job.addons.count=0;for(unsigned i=0;i<v.addons.count&&i<v.markedAddons.size();++i)if(v.markedAddons[i])job.addons.candidates[job.addons.count++]=v.addons.candidates[i];job.restoreWithAddons=job.addons.count!=0;}job.restorePermanent=job.restoreWithAddons&&v.restorePermanent;}
    if(kind==Kind::DiscardExtras)job.addons=v.addons;
    Result rc=threadCreate(&job.thread,work,nullptr,nullptr,512*1024,0x2C,-2);if(R_SUCCEEDED(rc)){rc=threadStart(&job.thread);if(R_FAILED(rc))threadClose(&job.thread);}
    if(R_FAILED(rc)){v.message="No se pudo iniciar la operación.";v.success=false;v.screen=Screen::Result;return false;}
    job.active=true;v.operation=label;v.screen=Screen::Busy;v.current=v.total=0;appletSetAutoSleepDisabled(true);appletLockExit();return true;
}
void applyRuntime(View&v){v.atmosphere=job.runtime.atmosphere;v.identityValid=job.runtime.identityValid;v.firmware=job.runtime.firmware;v.model=job.runtime.model;v.environmentDetail=job.runtime.detail;}
void showResult(View&v,bool ok,const std::string&text){v.success=ok;v.message=text.empty()?(ok?"Operación completada.":"No se pudo completar. No se ha confirmado un estado válido."):text;v.screen=Screen::Result;}
}
int main(int argc,char**argv){
    (void)argc;(void)argv;mutexInit(&job.progressMutex);View v;v.appletMode=appletGetAppletType()!=AppletType_Application&&appletGetAppletType()!=AppletType_SystemApplication;
    // Repair only the app's update journal before opening a RomFS backed by its NRO.
    const Runtime startup=inspectRuntime();if(startup.atmosphere)nm::recoverAppUpdate(AppPath,v.recoveryError);
    romfsMounted=R_SUCCEEDED(romfsInit());if(!romfsMounted||!uiInit("romfs:")){consoleInit(nullptr);std::printf("NinMagic Guardian: faltan recursos. Instala el ZIP completo.\nPulsa + para salir.\n");PadState p;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&p);while(appletMainLoop()){padUpdate(&p);if(padGetButtonsDown(&p)&HidNpadButton_Plus)break;consoleUpdate(nullptr);}consoleExit(nullptr);if(romfsMounted)romfsExit();return 1;}
    Framebuffer frame;Result rc=framebufferCreate(&frame,nwindowGetDefault(),1280,720,PIXEL_FORMAT_RGBA_8888,2);if(R_FAILED(rc)){uiExit();romfsExit();return 1;}framebufferMakeLinear(&frame);
    PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);bool quit=false;Screen resultReturn=Screen::Home;start(Kind::Init,v,"Comprobando microSD y respaldo sin restaurar archivos...");
    while(appletMainLoop()&&!quit){
        padUpdate(&pad);u64 down=padGetButtonsDown(&pad);v.seconds=double(armGetSystemTick())/armGetSystemTickFreq();bool completed=false;
        if(job.active){v.current=job.current.load(std::memory_order_relaxed);v.total=job.total.load(std::memory_order_relaxed);mutexLock(&job.progressMutex);v.progressPath=job.path;mutexUnlock(&job.progressMutex);}
        if(job.active&&job.done.load(std::memory_order_acquire)){
            threadWaitForExit(&job.thread);threadClose(&job.thread);job.active=false;appletSetAutoSleepDisabled(false);appletUnlockExit();completed=true;applyRuntime(v);
            if(job.kind==Kind::Create&&job.result.info.present)v.backup=job.result.info;
            if(job.kind==Kind::CheckUpdate){v.update=job.update;v.updateRecord=job.record;v.queryComplete=true;v.updateError=job.error;v.screen=Screen::Update;}
            else if(!job.ok)showResult(v,false,job.error);
            else switch(job.kind){
                case Kind::Init:v.options=job.options;v.backup=job.result.info;v.recoveryError=job.recoveryError;v.screen=Screen::Home;v.selected=0;if(!job.result.ok)showResult(v,false,job.result.message);break;
                case Kind::Scan:v.source=job.result.info;v.sourceReady=true;v.screen=Screen::BackupConfirm;break;
                case Kind::ReviewRestore:v.backup=job.result.info;v.addons=job.addons;v.restoreAddons=v.addons.ok&&(v.addons.count||v.addons.quarantine_pending||v.addons.discard_pending);v.restorePermanent=v.addons.discard_pending;v.markedAddons.assign(v.addons.count,v.restoreAddons);v.selected=0;v.screen=v.addons.count?Screen::RestoreChanges:Screen::RestoreConfirm;break;
                case Kind::Create:case Kind::Verify:case Kind::Restore:v.backup=job.result.info;showResult(v,true,job.message);break;
                case Kind::Options:v.screen=Screen::Home;v.selected=4;break;
                case Kind::InspectAddons:v.addons=job.addons;v.markedAddons.assign(v.addons.count,v.addons.quarantine_pending||v.addons.discard_pending);v.selected=0;v.screen=Screen::AddonReview;break;
                case Kind::Quarantine:case Kind::Reactivate:case Kind::DiscardExtras:showResult(v,true,job.message);break;
                case Kind::Rescue:showResult(v,true,job.message);break;
                case Kind::PrepareUpdate:v.update=job.update;v.screen=Screen::ConfirmUpdate;break;
                case Kind::InstallUpdate:v.update=job.update;v.updateRecord=job.record;showResult(v,true,job.message+" Cierra Guardian y vuelve a abrirla.");break;
                case Kind::Archive:v.recoveryError.clear();v.updateRecord={};showResult(v,true,job.message);break;
                case Kind::CheckUpdate:break;
            }
        }
        // A key pressed for the previous screen cannot confirm a newly shown write.
        if(!job.active&&!completed){
            if(down&HidNpadButton_Plus)quit=true;
            else switch(v.screen){
                case Screen::Home:
                    if(down&(HidNpadButton_Down|HidNpadButton_StickLDown))v.selected=(v.selected+1)%6;
                    else if(down&(HidNpadButton_Up|HidNpadButton_StickLUp))v.selected=(v.selected+5)%6;
                    else if(down&HidNpadButton_Minus)v.screen=Screen::Help;
                    else if(down&HidNpadButton_R){resultReturn=Screen::Update;start(Kind::CheckUpdate,v,"Consultando la versión publicada de Guardian...");}
                    else if(down&HidNpadButton_A){resultReturn=Screen::Home;switch(v.selected){case 0:start(Kind::Scan,v,"Calculando los archivos y el tamaño real del respaldo...");break;case 1:start(Kind::Verify,v,"Verificando el manifiesto y cada archivo del respaldo...");break;case 2:start(Kind::ReviewRestore,v,"Comprobando la copia definitiva antes de revisar la restauración...");break;case 3:v.screen=Screen::RescueConfirm;break;case 4:v.screen=Screen::Options;v.selected=0;break;case 5:start(Kind::InspectAddons,v,"Comparando temas y módulos con el inventario del respaldo...");break;}}
                    break;
                case Screen::Options:
                    if(down&(HidNpadButton_Down|HidNpadButton_Up|HidNpadButton_StickLDown|HidNpadButton_StickLUp))v.selected=1-v.selected;
                    else if(down&HidNpadButton_A)v.options^=v.selected?GC_OPT_PAYLOADS:GC_OPT_VISUALS;
                    else if(down&HidNpadButton_B){resultReturn=Screen::Home;start(Kind::Options,v,"Guardando la selección de extras...");}
                    break;
                case Screen::BackupConfirm:if(down&HidNpadButton_B)v.screen=Screen::Home;else if(down&HidNpadButton_A){resultReturn=Screen::Home;start(Kind::Create,v,"Creando y verificando el nuevo respaldo. La copia anterior se conserva hasta terminar...");}
                    break;
                case Screen::RestoreConfirm:
                    if(down&HidNpadButton_B)v.screen=Screen::Home;
                    else if((down&HidNpadButton_X)&&v.addons.ok&&v.addons.count){v.screen=Screen::RestoreChanges;v.selected=0;}
                    else if((down&HidNpadButton_Y)&&v.addons.ok&&v.addons.count&&!v.addons.discard_pending){if(!v.restoreAddons){v.restoreAddons=true;v.restorePermanent=false;v.markedAddons.assign(v.addons.count,true);}else if(!v.restorePermanent)v.restorePermanent=true;else{v.restoreAddons=false;v.restorePermanent=false;v.markedAddons.assign(v.addons.count,false);}}
                    else if(down&HidNpadButton_A){if(v.restoreAddons&&v.restorePermanent&&!v.addons.discard_pending){v.discardWithRestore=true;v.screen=Screen::DiscardConfirm;v.selected=0;}else{resultReturn=Screen::Home;start(Kind::Restore,v,v.restoreAddons?(v.restorePermanent?"Continuando la recuperación y eliminación ya confirmadas...":"Apartando los cambios de arranque confirmados y restaurando la copia..."):"Restaurando los archivos del respaldo sin apartar cambios nuevos...");}}
                    break;
                case Screen::RestoreChanges:
                    if((down&(HidNpadButton_Down|HidNpadButton_StickLDown))&&v.addons.count)v.selected=(v.selected+1)%v.addons.count;
                    else if((down&(HidNpadButton_Up|HidNpadButton_StickLUp))&&v.addons.count)v.selected=(v.selected+v.addons.count-1)%v.addons.count;
                    else if((down&HidNpadButton_A)&&!v.addons.quarantine_pending&&!v.addons.discard_pending&&v.selected<v.markedAddons.size()){v.markedAddons[v.selected]=!v.markedAddons[v.selected];v.restoreAddons=std::any_of(v.markedAddons.begin(),v.markedAddons.end(),[](bool x){return x;});}
                    else if((down&HidNpadButton_X)&&!v.addons.quarantine_pending&&!v.addons.discard_pending){const bool all=std::all_of(v.markedAddons.begin(),v.markedAddons.end(),[](bool x){return x;});v.markedAddons.assign(v.addons.count,!all);v.restoreAddons=!all;}
                    else if(down&(HidNpadButton_B|HidNpadButton_Y)){v.screen=Screen::RestoreConfirm;v.selected=0;}
                    break;
                case Screen::DiscardConfirm:
                    if((down&(HidNpadButton_Down|HidNpadButton_StickLDown))&&v.addons.count)v.selected=(v.selected+1)%v.addons.count;
                    else if((down&(HidNpadButton_Up|HidNpadButton_StickLUp))&&v.addons.count)v.selected=(v.selected+v.addons.count-1)%v.addons.count;
                    else if(down&HidNpadButton_B){v.screen=v.discardWithRestore?Screen::RestoreConfirm:Screen::AddonReview;v.selected=0;}
                    else if(down&HidNpadButton_X){resultReturn=Screen::Home;if(v.discardWithRestore)start(Kind::Restore,v,"Restaurando y eliminando definitivamente los extras confirmados...");else start(Kind::DiscardExtras,v,"Eliminando definitivamente los extras apartados del plan confirmado...");}
                    break;
                case Screen::RescueConfirm:if(down&HidNpadButton_B)v.screen=Screen::Home;else if(down&HidNpadButton_A){resultReturn=Screen::Home;start(Kind::Rescue,v,"Preparando y verificando el acceso independiente de Hekate...");}
                    break;
                case Screen::AddonReview:
                    if((down&(HidNpadButton_Down|HidNpadButton_StickLDown))&&v.addons.count)v.selected=(v.selected+1)%v.addons.count;
                    else if((down&(HidNpadButton_Up|HidNpadButton_StickLUp))&&v.addons.count)v.selected=(v.selected+v.addons.count-1)%v.addons.count;
                    else if((down&HidNpadButton_A)&&v.addons.ok&&!v.addons.quarantine_pending&&!v.addons.discard_pending&&v.selected<v.markedAddons.size())v.markedAddons[v.selected]=!v.markedAddons[v.selected];
                    else if((down&HidNpadButton_Y)&&v.addons.ok&&!v.addons.discard_pending&&(v.addons.quarantine_pending||std::any_of(v.markedAddons.begin(),v.markedAddons.end(),[](bool x){return x;})))v.screen=Screen::QuarantineConfirm;
                    else if((down&HidNpadButton_X)&&!v.addons.discard_pending)v.screen=Screen::ReactivateConfirm;
                    else if((down&HidNpadButton_R)&&v.addons.ok&&(v.addons.quarantine_pending||v.addons.discard_pending)&&v.addons.count){v.discardWithRestore=false;v.screen=Screen::DiscardConfirm;v.selected=0;}
                    else if(down&HidNpadButton_B){v.screen=Screen::Home;v.selected=5;}
                    break;
                case Screen::QuarantineConfirm:if(down&HidNpadButton_B)v.screen=Screen::AddonReview;else if(down&HidNpadButton_A){resultReturn=Screen::Home;start(Kind::Quarantine,v,"Apartando únicamente los complementos de arranque confirmados...");}break;
                case Screen::ReactivateConfirm:if(down&HidNpadButton_B)v.screen=Screen::AddonReview;else if(down&HidNpadButton_A){resultReturn=Screen::Home;start(Kind::Reactivate,v,"Recuperando los complementos apartados sin sustituir archivos nuevos...");}break;
                case Screen::Help:if(down&HidNpadButton_B)v.screen=Screen::Home;break;
                case Screen::Result:if(down&HidNpadButton_B){if(v.restartRequired)quit=true;else {v.screen=resultReturn;v.selected=0;}}
                    break;
                case Screen::Update:
                    if(down&HidNpadButton_B){v.screen=Screen::Home;v.selected=0;}
                    else if(down&HidNpadButton_X){resultReturn=Screen::Update;start(Kind::CheckUpdate,v,"Comprobando el canal HTTPS de Guardian...");}
                    else if((down&HidNpadButton_A)&&canDownloadAppUpdate(v)){resultReturn=Screen::Update;if(v.update.prepared)v.screen=Screen::ConfirmUpdate;else start(Kind::PrepareUpdate,v,"Descargando y verificando la actualización del NRO...");}
                    else if((down&HidNpadButton_Y)&&v.updateRecord.state==nm::CompletedUpdateState::CanArchive)v.screen=Screen::ConfirmArchive;
                    break;
                case Screen::ConfirmUpdate:
                    if(down&HidNpadButton_B)v.screen=Screen::Update;
                    else if(down&HidNpadButton_A){if(romfsMounted){romfsExit();romfsMounted=false;}v.restartRequired=true;resultReturn=Screen::Update;start(Kind::InstallUpdate,v,"Instalando el NRO verificado. El respaldo y el rescatador se conservan...");}
                    break;
                case Screen::ConfirmArchive:if(down&HidNpadButton_B)v.screen=Screen::Update;else if(down&HidNpadButton_A){resultReturn=Screen::Update;start(Kind::Archive,v,"Archivando el registro finalizado de actualización...");}
                    break;
                case Screen::Busy:break;
            }
        }
        u32 stride=0;auto*pixels=static_cast<u32*>(framebufferBegin(&frame,&stride));uiDraw(pixels,int(stride/4),v);framebufferEnd(&frame);
    }
    if(job.active){threadWaitForExit(&job.thread);threadClose(&job.thread);appletUnlockExit();}appletSetAutoSleepDisabled(false);nm::updateNetworkExit();uiExit();framebufferClose(&frame);if(romfsMounted)romfsExit();return 0;
}
