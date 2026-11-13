#include "rescue_install.hpp"
#include "guardian_sha256.h"
#include <array>
#include <cstring>
#include <fstream>
#include <vector>
namespace nmguardian {
namespace {
bool parents(gc_fs&f,const std::string&p){for(size_t at=p.find('/');at!=std::string::npos;at=p.find('/',at+1)){auto parent=p.substr(0,at);gc_stat s{};int st=f.stat(f.user,parent.c_str(),&s);if(st<0||(st==1&&s.type!=GC_DIR))return false;if(!st&&f.mkdir(f.user,parent.c_str()))return false;}return true;}
bool read(gc_fs&f,const std::string&p,std::vector<unsigned char>&out,size_t limit){gc_stat s{};if(f.stat(f.user,p.c_str(),&s)!=1||s.type!=GC_FILE||s.size>limit)return false;auto*h=f.open(f.user,p.c_str(),'r');if(!h)return false;out.resize(static_cast<size_t>(s.size));size_t at=0;bool ok=true;while(at<out.size()){size_t n=0;if(f.read(f.user,h,out.data()+at,out.size()-at,&n)||!n){ok=false;break;}at+=n;}unsigned char extra;size_t n=0;if(ok&&(f.read(f.user,h,&extra,1,&n)||n))ok=false;if(f.close(f.user,h))ok=false;return ok;}
bool digestEqual(const std::vector<unsigned char>&a,const std::vector<unsigned char>&b){if(a.size()!=b.size())return false;unsigned char ah[32],bh[32];gc_sha256_buffer(a.data(),a.size(),ah);gc_sha256_buffer(b.data(),b.size(),bh);return !std::memcmp(ah,bh,32);}
bool installMissing(gc_fs&f,const std::string&path,const std::vector<unsigned char>&data,std::string&error){
    gc_stat s{};int st=f.stat(f.user,path.c_str(),&s);if(st<0){error="No se pudo leer la ruta del rescatador: "+path;return false;}
    if(st){std::vector<unsigned char>current;if(!read(f,path,current,4*1024*1024)||!digestEqual(data,current)){error="Se conserva un archivo distinto: "+path+". Instala el ZIP completo de Guardian para actualizar el rescatador.";return false;}return true;}
    if(!parents(f,path)){error="No se puede crear la carpeta de rescate. Si Hekate protege bootloader, prepara el acceso desde el ZIP en el PC.";return false;}
    std::string temp;for(unsigned i=0;i<100;++i){temp=path+".guardian-new-"+std::to_string(i);int v=f.stat(f.user,temp.c_str(),&s);if(v<0)return false;if(!v)break;if(i==99){error="Hay archivos temporales del rescatador pendientes de revisión.";return false;}}
    auto*h=f.open(f.user,temp.c_str(),'x');if(!h){error="No se pudo crear el archivo de rescate.";return false;}
    bool ok=!f.write(f.user,h,data.data(),data.size());if(ok)ok=!f.file_sync(f.user,h);if(f.close(f.user,h))ok=false;
    std::vector<unsigned char>verify;if(ok)ok=read(f,temp,verify,4*1024*1024)&&digestEqual(data,verify);
    if(ok)ok=!f.rename(f.user,temp.c_str(),path.c_str());
    if(ok)ok=!f.sync(f.user);
    if(!ok){f.remove(f.user,temp.c_str(),GC_FILE);error="La preparación no terminó. Los archivos anteriores se conservan. Vuelve a preparar el acceso.";}return ok;
}
std::vector<unsigned char> embedded(const std::string&p){std::ifstream in(p,std::ios::binary);if(!in)return{};in.seekg(0,std::ios::end);auto n=in.tellg();if(n<=0||n>4*1024*1024)return{};in.seekg(0);std::vector<unsigned char>b(static_cast<size_t>(n));in.read(reinterpret_cast<char*>(b.data()),n);return in?b:std::vector<unsigned char>{};}
}
bool prepareRescue(gc_fs f,const std::string&root,std::string&message){
    const std::array<const char*,3>names={"ninmagic-guardian-rescue.bin","libsys_minerva.bso","ninmagic-guardian.ini"};
    const std::array<const char*,3>dest={"config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin","config/NinMagicGuardian/rescue/libsys_minerva.bso","bootloader/ini/ninmagic-guardian.ini"};
    std::array<std::vector<unsigned char>,3>data;for(size_t i=0;i<names.size();++i){data[i]=embedded(root+"/rescue/"+names[i]);if(data[i].empty()){message="Faltan recursos del rescatador dentro de esta aplicación. Usa el ZIP completo de Guardian.";return false;}}
    // Check every existing destination before making any change.
    for(size_t i=0;i<dest.size();++i){gc_stat s{};int st=f.stat(f.user,dest[i],&s);if(st<0){message="No se pudo comprobar el acceso del rescatador.";return false;}if(st){std::vector<unsigned char>b;if(!read(f,dest[i],b,4*1024*1024)||!digestEqual(data[i],b)){message="Se conserva un recurso distinto: "+std::string(dest[i])+". Instala el ZIP completo para actualizar el rescate.";return false;}}}
    // The boot entry is promoted last, after the independent payload/resources.
    for(size_t i=0;i<dest.size();++i)if(!installMissing(f,dest[i],data[i],message))return false;
    message="Acceso preparado y archivos verificados. En Hekate entra en More configs > NinMagic Guardian Rescate. Usa VOL+/VOL- y POWER. Primero comprueba el respaldo sin restaurar. El arranque físico de esta primera beta necesita prueba en tu consola.";return true;
}
unsigned loadOptions(gc_fs f){std::vector<unsigned char>b;const char*paths[]={"config/NinMagicGuardian/options.txt","config/NinMagicGuardian/options.previous"};for(auto*p:paths)if(read(f,p,b,8)&&b.size()==2&&b[0]>='0'&&b[0]<='3'&&b[1]=='\n')return unsigned(b[0]-'0');return 0;}
bool saveOptions(gc_fs f,unsigned options,std::string&error){
    const std::string p="config/NinMagicGuardian/options.txt",old="config/NinMagicGuardian/options.previous",temp="config/NinMagicGuardian/options.new";gc_stat s{};if(options>3||!parents(f,p)){error="No se pudieron guardar los extras.";return false;}
    // Review every existing path before deleting an older fallback or cache.
    for(const auto&v:{p,temp,old}){int st=f.stat(f.user,v.c_str(),&s);if(st<0||(st&&s.type!=GC_FILE)){error="Configuración pendiente de revisión.";return false;}}
    for(const auto&v:{temp,old}){int st=f.stat(f.user,v.c_str(),&s);if(st<0||(st&&f.remove(f.user,v.c_str(),GC_FILE))){error="No se pudo renovar la configuración.";return false;}}
    const unsigned char data[]={static_cast<unsigned char>('0'+options),'\n'};auto*h=f.open(f.user,temp.c_str(),'x');if(!h)return false;bool ok=!f.write(f.user,h,data,2)&&!f.file_sync(f.user,h);if(f.close(f.user,h))ok=false;
    int st=f.stat(f.user,p.c_str(),&s);if(st<0||(st&&s.type!=GC_FILE))ok=false;if(ok&&st)ok=!f.rename(f.user,p.c_str(),old.c_str());if(ok)ok=!f.rename(f.user,temp.c_str(),p.c_str());if(ok)ok=!f.sync(f.user);if(!ok){error="No se pudieron confirmar los extras. El respaldo no ha cambiado.";return false;}return true;
}
}
