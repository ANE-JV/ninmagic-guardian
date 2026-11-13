#include "ui.hpp"
#include <cstdio>
#include <vector>
int main(int argc,char**argv){if(argc!=3||!nmguardian::uiInit(argv[1]))return 1;nmguardian::View v;v.atmosphere=true;v.identityValid=true;v.firmware="23.0.1";v.model="Vista previa / datos de ejemplo";v.backup.present=v.backup.verified=true;v.backup.files=83;v.backup.bytes=87234567;std::snprintf(v.backup.created,sizeof(v.backup.created),"Ejemplo de copia verificada");std::vector<uint32_t>px(1280*720);nmguardian::uiDraw(px.data(),1280,v);auto*f=std::fopen(argv[2],"wb");if(!f)return 2;std::fprintf(f,"P6\n1280 720\n255\n");for(auto p:px){unsigned char rgb[]={static_cast<unsigned char>(p),static_cast<unsigned char>(p>>8),static_cast<unsigned char>(p>>16)};std::fwrite(rgb,1,3,f);}std::fclose(f);nmguardian::uiExit();return 0;}
