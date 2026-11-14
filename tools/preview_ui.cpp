#include "ui.hpp"
#include <cstdio>
#include <cstring>
#include <vector>
int main(int argc,char**argv){
    if((argc!=3&&argc!=4)||!nmguardian::uiInit(argv[1]))return 1;
    nmguardian::View v;v.atmosphere=true;v.identityValid=true;v.firmware="23.0.1";
    v.model="Vista previa / datos de ejemplo";v.options=0;
    v.backup.present=v.backup.verified=true;v.backup.files=83;v.backup.bytes=87234567;v.backup.options=0;
    std::snprintf(v.backup.created,sizeof(v.backup.created),"Ejemplo de copia verificada");std::snprintf(v.backup.firmware,sizeof(v.backup.firmware),"23.0.1 (ejemplo)");v.source=v.backup;
    v.addons.ok=1;v.addons.count=2;v.addons.candidates[0].kind=GA_BOOT_MODULE;v.addons.candidates[0].newly_added=1;std::snprintf(v.addons.candidates[0].title_id,17,"0100000000000123");v.addons.candidates[1].kind=GA_HOME_THEME;std::snprintf(v.addons.candidates[1].title_id,17,"0100000000001000");v.restoreAddons=true;v.markedAddons.assign(2,true);
    if(argc==4){if(!std::strcmp(argv[3],"options")){v.screen=nmguardian::Screen::Options;v.selected=1;}else if(!std::strcmp(argv[3],"backup"))v.screen=nmguardian::Screen::BackupConfirm;else if(!std::strcmp(argv[3],"restore"))v.screen=nmguardian::Screen::RestoreConfirm;else if(!std::strcmp(argv[3],"changes"))v.screen=nmguardian::Screen::RestoreChanges;else if(!std::strcmp(argv[3],"delete")){v.screen=nmguardian::Screen::DiscardConfirm;v.discardWithRestore=true;v.restorePermanent=true;}else if(!std::strcmp(argv[3],"discard")){v.screen=nmguardian::Screen::DiscardConfirm;v.discardWithRestore=false;}else if(!std::strcmp(argv[3],"permanent")){v.screen=nmguardian::Screen::RestoreConfirm;v.restorePermanent=true;}else if(!std::strcmp(argv[3],"files")){v.screen=nmguardian::Screen::RestoreConfirm;v.restoreAddons=false;}else return 2;}
    std::vector<uint32_t>px(1280*720);nmguardian::uiDraw(px.data(),1280,v);auto*f=std::fopen(argv[2],"wb");if(!f)return 2;
    std::fprintf(f,"P6\n1280 720\n255\n");for(auto pixel:px){unsigned char rgb[]={static_cast<unsigned char>(pixel),static_cast<unsigned char>(pixel>>8),static_cast<unsigned char>(pixel>>16)};std::fwrite(rgb,1,3,f);}
    std::fclose(f);nmguardian::uiExit();return 0;
}
