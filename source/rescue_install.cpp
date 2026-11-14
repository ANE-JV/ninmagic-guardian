// SPDX-License-Identifier: GPL-2.0-only
#include "rescue_install.hpp"
#include "guardian_sha256.h"
#include <array>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace nmguardian {
namespace {
constexpr uint64_t Limit=4*1024*1024;
constexpr const char *Root="config/NinMagicGuardian/rescue-update";
constexpr const char *Active="config/NinMagicGuardian/rescue-update/active.txt";
const std::array<const char*,3> Names={"ninmagic-guardian-rescue.bin","libsys_minerva.bso","ninmagic-guardian.ini"};
// Resource pair first, independent menu entry last. Never the main boot config.
const std::array<const char*,3> Targets={"config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin","config/NinMagicGuardian/rescue/libsys_minerva.bso","bootloader/ini/ninmagic-guardian.ini"};
using Bytes=std::vector<unsigned char>;
bool fail(std::string &error,const std::string &text){error=text;return false;}
bool parents(gc_fs &f,const std::string &path){
    for(size_t at=path.find('/');at!=std::string::npos;at=path.find('/',at+1)){
        const auto parent=path.substr(0,at);gc_stat st{};const int found=f.stat(f.user,parent.c_str(),&st);
        if(found<0||(found&&st.type!=GC_DIR)||(!found&&f.mkdir(f.user,parent.c_str())))return false;
    }
    return true;
}
bool read(gc_fs &f,const std::string &path,Bytes &out,uint64_t limit){
    gc_stat st{};if(f.stat(f.user,path.c_str(),&st)!=1||st.type!=GC_FILE||st.size>limit)return false;
    void *file=f.open(f.user,path.c_str(),'r');if(!file)return false;
    out.resize(static_cast<size_t>(st.size));size_t position=0;bool ok=true;
    while(position<out.size()){size_t count=0;if(f.read(f.user,file,out.data()+position,out.size()-position,&count)||!count||count>out.size()-position){ok=false;break;}position+=count;}
    unsigned char extra{};size_t count=0;if(ok&&(f.read(f.user,file,&extra,1,&count)||count))ok=false;
    if(f.close(f.user,file))ok=false;
    return ok;
}
std::string digest(const void *data,size_t size){
    unsigned char hash[32];gc_sha256_buffer(data,size,hash);constexpr char hex[]="0123456789abcdef";std::string out;
    for(auto byte:hash){out+=hex[byte>>4];out+=hex[byte&15];}return out;
}
std::string digest(const Bytes &data){return digest(data.data(),data.size());}
bool hashValid(const std::string &hash){return hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==std::string::npos;}
bool number(const std::string &text,uint64_t &out){
    if(text.empty()||text.size()>20)return false;
    out=0;for(unsigned char c:text){if(c<'0'||c>'9'||out>(UINT64_MAX-(c-'0'))/10)return false;out=out*10+c-'0';}return true;
}
bool matches(gc_fs &f,const std::string &path,uint64_t size,const std::string &hash){Bytes bytes;return read(f,path,bytes,Limit)&&bytes.size()==size&&digest(bytes)==hash;}
bool writeNew(gc_fs &f,const std::string &path,const Bytes &bytes,std::string &error){
    if(!parents(f,path))return fail(error,"No se puede crear una carpeta propia del rescatador.");
    gc_stat st{};if(f.stat(f.user,path.c_str(),&st)!=0)return fail(error,"La preparación ya existe o no es segura: "+path);
    void *file=f.open(f.user,path.c_str(),'x');if(!file)return fail(error,"No se pudo crear la preparación: "+path);
    bool ok=true;for(size_t offset=0;offset<bytes.size();){const auto count=std::min<size_t>(65536,bytes.size()-offset);if(f.write(f.user,file,bytes.data()+offset,count)){ok=false;break;}offset+=count;}
    if(ok&&f.file_sync(f.user,file))ok=false;
    if(f.close(f.user,file))ok=false;
    if(!ok||!matches(f,path,bytes.size(),digest(bytes)))return fail(error,"No se pudo guardar y verificar la preparación. Los rescatadores anteriores se conservan.");
    return true;
}
bool sync(gc_fs &f,std::string &error){return f.sync(f.user)==0||fail(error,"No se pudieron confirmar los cambios del rescatador. Repite Preparar rescate para continuar.");}
bool checkpoint(gc_fs &f,const char *point,const std::string &path,std::string &error){return !f.checkpoint||!f.checkpoint(f.user,point,path.c_str())||fail(error,"Preparación interrumpida. Repite la operación para continuar; los rescatadores anteriores se conservan.");}
Bytes embedded(const std::string &path){
    std::ifstream file(path,std::ios::binary);if(!file)return{};file.seekg(0,std::ios::end);const auto size=file.tellg();if(size<=0||uint64_t(size)>Limit)return{};file.seekg(0);Bytes bytes(static_cast<size_t>(size));file.read(reinterpret_cast<char*>(bytes.data()),size);return file?bytes:Bytes{};
}
struct Entry {bool hadOld=false;uint64_t oldSize=0,newSize=0;std::string oldHash="-",newHash;};
struct Plan {std::string generation;std::array<Entry,3> entries;};
std::string generationRoot(const Plan &p){return std::string(Root)+"/generations/"+p.generation;}
std::string staged(const Plan &p,size_t i){return generationRoot(p)+"/incoming/"+std::to_string(i)+".bin";}
std::string previous(const Plan &p,size_t i){return generationRoot(p)+"/previous/"+std::to_string(i)+".bin";}
std::string retired(const Plan &p,size_t i){return generationRoot(p)+"/retired/"+std::to_string(i)+".bin";}
std::string temporary(const Plan &p,size_t i){return std::string(Targets[i])+".guardian-new-"+p.generation;}
std::string planText(const Plan &p){
    std::string out="NMGRESCUE2\ngeneration="+p.generation+"\n";
    for(size_t i=0;i<p.entries.size();++i){const auto &e=p.entries[i];out+=std::to_string(i)+"|"+(e.hadOld?"1":"0")+"|"+std::to_string(e.oldSize)+"|"+e.oldHash+"|"+std::to_string(e.newSize)+"|"+e.newHash+"\n";}
    return out+"sha256="+digest(out.data(),out.size())+"\n";
}
bool parsePlan(const Bytes &bytes,Plan &p){
    std::string text(bytes.begin(),bytes.end());const auto seal=text.rfind("sha256=");
    if(seal==std::string::npos||text.size()-seal!=72||text.back()!='\n'||text.find('\0')!=std::string::npos||digest(text.data(),seal)!=text.substr(seal+7,64))return false;
    std::istringstream input(text.substr(0,seal));std::string line;
    if(!std::getline(input,line)||line!="NMGRESCUE2"||!std::getline(input,line)||line.rfind("generation=",0)!=0)return false;
    p={};p.generation=line.substr(11);if(p.generation.size()!=8||p.generation=="00000000"||p.generation.find_first_not_of("0123456789")!=std::string::npos)return false;
    for(size_t i=0;i<p.entries.size();++i){
        if(!std::getline(input,line))return false;
        std::istringstream row(line);std::vector<std::string> fields;std::string field;while(std::getline(row,field,'|'))fields.push_back(field);
        if(fields.size()!=6||fields[0]!=std::to_string(i)||(fields[1]!="0"&&fields[1]!="1"))return false;
        auto &e=p.entries[i];e.hadOld=fields[1]=="1";e.oldHash=fields[3];e.newHash=fields[5];
        if(!number(fields[2],e.oldSize)||!number(fields[4],e.newSize)||e.oldSize>Limit||!e.newSize||e.newSize>Limit||!hashValid(e.newHash))return false;
        if(e.hadOld?!hashValid(e.oldHash):(e.oldSize!=0||e.oldHash!="-"))return false;
    }
    return !std::getline(input,line)&&text==planText(p);
}
struct GenerationScan {unsigned maximum=0;bool valid=true;};
int generationVisit(void *user,const char *name,gc_type type){
    auto &scan=*static_cast<GenerationScan*>(user);const std::string value(name);
    if(value.size()==8&&value.find_first_not_of("0123456789")==std::string::npos){if(type!=GC_DIR){scan.valid=false;return -1;}uint64_t parsed=0;if(!number(value,parsed)){scan.valid=false;return -1;}scan.maximum=std::max(scan.maximum,static_cast<unsigned>(parsed));}return 0;
}
bool nextGeneration(gc_fs &f,Plan &p,std::string &error){
    const auto root=std::string(Root)+"/generations";if(!parents(f,root+"/entry"))return fail(error,"No se puede preparar el historial del rescatador.");
    GenerationScan scan;if(f.list(f.user,root.c_str(),generationVisit,&scan)||!scan.valid||scan.maximum>=99999999)return fail(error,"No se pudo numerar una copia anterior del rescatador sin sobrescribirla.");
    char id[9];std::snprintf(id,sizeof id,"%08u",scan.maximum+1);p.generation=id;return true;
}
bool inspectTargets(gc_fs &f,Plan &p,const std::array<Bytes,3> &assets,bool &equal,std::string &error){
    equal=true;
    for(size_t i=0;i<p.entries.size();++i){
        auto &e=p.entries[i];e.newSize=assets[i].size();e.newHash=digest(assets[i]);gc_stat st{};const int found=f.stat(f.user,Targets[i],&st);
        if(found<0||(found&&(st.type!=GC_FILE||st.size>Limit)))return fail(error,"Se conserva un destino del rescatador que no es un archivo normal: "+std::string(Targets[i]));
        e.hadOld=found!=0;if(e.hadOld){Bytes old;if(!read(f,Targets[i],old,Limit))return fail(error,"No se pudo leer el rescatador anterior; no se cambia nada.");e.oldSize=old.size();e.oldHash=digest(old);}
        if(!e.hadOld||e.oldSize!=e.newSize||e.oldHash!=e.newHash)equal=false;
    }
    return true;
}
bool preparePlan(gc_fs &f,Plan &p,const std::array<Bytes,3> &assets,std::string &error){
    if(!nextGeneration(f,p,error))return false;
    // Every previous and incoming file is sealed before publishing the active
    // journal. No live destination is renamed before this boundary.
    for(size_t i=0;i<p.entries.size();++i){const auto &e=p.entries[i];
        if(e.hadOld){Bytes old;if(!read(f,Targets[i],old,Limit)||old.size()!=e.oldSize||digest(old)!=e.oldHash)return fail(error,"El rescatador cambió durante la preparación; se conserva sin sustituirlo.");if(!writeNew(f,previous(p,i),old,error))return false;}
        if(!writeNew(f,staged(p,i),assets[i],error)||!checkpoint(f,"rescue.staged",Targets[i],error))return false;
    }
    const auto text=planText(p);const Bytes bytes(text.begin(),text.end());const auto pending=generationRoot(p)+"/plan.prepared.txt";
    if(!writeNew(f,pending,bytes,error)||!sync(f,error)||!parents(f,Active)||f.rename(f.user,pending.c_str(),Active)||!sync(f,error))return fail(error,error.empty()?"No se pudo confirmar el plan del rescatador. Los anteriores se conservan.":error);
    return checkpoint(f,"rescue.journal",Active,error);
}
bool resumePlan(gc_fs &f,const Plan &p,std::string &error){
    // Preflight the complete generation, including immutable prior copies.
    for(size_t i=0;i<p.entries.size();++i){const auto &e=p.entries[i];if(!matches(f,staged(p,i),e.newSize,e.newHash)||(e.hadOld&&!matches(f,previous(p,i),e.oldSize,e.oldHash)))return fail(error,"El plan del rescatador tiene una copia incompleta o modificada. Se conserva todo para revisión.");}
    // A conflict at the last destination must be discovered before changing
    // either member of the resource pair. The checks repeat before each rename.
    for(size_t i=0;i<p.entries.size();++i){const auto &e=p.entries[i];gc_stat live{},old{};const int found=f.stat(f.user,Targets[i],&live),saved=f.stat(f.user,retired(p,i).c_str(),&old);
        if(found<0||saved<0||(found&&live.type!=GC_FILE)||(saved&&(old.type!=GC_FILE||!e.hadOld||!matches(f,retired(p,i),e.oldSize,e.oldHash))))return fail(error,"Una ruta del plan del rescatador necesita revisión. No se cambia ningún destino nuevo.");
        gc_stat temp{};const int prepared=f.stat(f.user,temporary(p,i).c_str(),&temp);if(prepared<0||(prepared&&temp.type!=GC_FILE))return fail(error,"Un temporal del rescatador no es un archivo normal; se conserva sin cambiar ningún destino nuevo.");
        if(found&&matches(f,Targets[i],e.newSize,e.newHash))continue;
        if((found&&(!e.hadOld||saved||!matches(f,Targets[i],e.oldSize,e.oldHash)))||(!found&&e.hadOld&&!saved))return fail(error,"El conjunto del rescatador cambió después de confirmarlo; se conserva sin sobrescribir.");
    }
    for(size_t i=0;i<p.entries.size();++i){const auto &e=p.entries[i];gc_stat st{};const int found=f.stat(f.user,Targets[i],&st);
        if(found<0||(found&&st.type!=GC_FILE))return fail(error,"La ruta del rescatador dejó de ser un archivo normal; se conserva el plan.");
        if(found&&matches(f,Targets[i],e.newSize,e.newHash))continue;
        if(found&&(!e.hadOld||!matches(f,Targets[i],e.oldSize,e.oldHash)))return fail(error,"Un archivo del rescatador cambió después de confirmarlo; no se sobrescribe.");
        gc_stat oldSt{};const int oldFound=f.stat(f.user,retired(p,i).c_str(),&oldSt);
        if(oldFound<0||(oldFound&&(oldSt.type!=GC_FILE||!e.hadOld||!matches(f,retired(p,i),e.oldSize,e.oldHash))))return fail(error,"La copia retirada del rescatador necesita revisión; no se sobrescribe.");
        if((found&&oldFound)||(!found&&e.hadOld&&!oldFound))return fail(error,"El estado del reemplazo del rescatador cambió. Se conservan el plan y los anteriores.");
        const auto temp=temporary(p,i);gc_stat tempSt{};const int hasTemp=f.stat(f.user,temp.c_str(),&tempSt);
        if(hasTemp<0||(hasTemp&&tempSt.type!=GC_FILE))return fail(error,"Un temporal del rescatador no es un archivo normal; se conserva.");
        if(hasTemp&&!matches(f,temp,e.newSize,e.newHash)&&f.remove(f.user,temp.c_str(),GC_FILE))return fail(error,"No se pudo renovar un temporal incompleto del rescatador.");
        if(!matches(f,temp,e.newSize,e.newHash)){Bytes data;if(!read(f,staged(p,i),data,Limit)||!writeNew(f,temp,data,error))return false;}
        if(!checkpoint(f,"rescue.ready",Targets[i],error))return false;
        if(found){if(!parents(f,retired(p,i))||f.rename(f.user,Targets[i],retired(p,i).c_str())||!sync(f,error))return fail(error,error.empty()?"No se pudo conservar el archivo anterior del rescatador.":error);if(!checkpoint(f,"rescue.retired",Targets[i],error))return false;}
        if(f.rename(f.user,temp.c_str(),Targets[i])||!sync(f,error)||!matches(f,Targets[i],e.newSize,e.newHash))return fail(error,error.empty()?"No se pudo confirmar el rescatador nuevo. Repite Preparar rescate para continuar.":error);
        if(!checkpoint(f,"rescue.promoted",Targets[i],error))return false;
    }
    for(size_t i=0;i<p.entries.size();++i)if(!matches(f,Targets[i],p.entries[i].newSize,p.entries[i].newHash))return fail(error,"No se pudo verificar el conjunto completo del rescatador.");
    Bytes active;Plan current;if(!read(f,Active,active,4096)||!parsePlan(active,current)||planText(current)!=planText(p))return fail(error,"El registro del rescatador cambió; se conserva para revisión.");
    const auto completed=generationRoot(p)+"/completed.txt";
    if(f.rename(f.user,Active,completed.c_str())||!sync(f,error))return fail(error,error.empty()?"Rescatador instalado, pero falta cerrar su registro. Repite la operación.":error);
    return checkpoint(f,"rescue.complete",completed,error);
}
bool parseOptions(const Bytes &bytes,unsigned &mask){
    const std::string text(bytes.begin(),bytes.end());
    if(text.size()==2&&text[0]>='0'&&text[0]<='3'&&text[1]=='\n'){mask=unsigned(text[0]-'0');return true;}
    return false;
}
}
bool prepareRescue(gc_fs f,const std::string &root,std::string &message){
    message.clear();if(!f.stat||!f.list||!f.open||!f.read||!f.write||!f.file_sync||!f.close||!f.mkdir||!f.rename||!f.remove||!f.sync)return fail(message,"No se puede confirmar el escritor del rescatador.");
    std::array<Bytes,3> assets;for(size_t i=0;i<Names.size();++i){assets[i]=embedded(root+"/rescue/"+Names[i]);if(assets[i].empty())return fail(message,"Faltan recursos del rescatador dentro de esta aplicación. Usa el ZIP completo de Guardian.");}
    gc_stat activeSt{};const int active=f.stat(f.user,Active,&activeSt);
    if(active<0||(active&&activeSt.type!=GC_FILE))return fail(message,"El registro del rescatador no es un archivo normal. Se conserva sin modificarlo.");
    if(active){Bytes data;Plan pending;if(!read(f,Active,data,4096)||!parsePlan(data,pending))return fail(message,"El registro del rescatador está incompleto o modificado. Se conserva todo para revisión.");if(!resumePlan(f,pending,message))return false;}
    Plan plan;bool equal=false;if(!inspectTargets(f,plan,assets,equal,message))return false;
    if(!equal&&(!preparePlan(f,plan,assets,message)||!resumePlan(f,plan,message)))return false;
    message="Rescatador compatible instalado y verificado. Los anteriores se conservan en config/NinMagicGuardian/rescue-update. En Hekate entra en More configs > NinMagic Guardian Rescate. Primero comprueba el respaldo sin restaurar.";return true;
}
unsigned loadOptions(gc_fs f){
    for(const char *path:{"config/NinMagicGuardian/options.txt","config/NinMagicGuardian/options.previous"}){gc_stat st{};Bytes bytes;unsigned mask=0;if(f.stat(f.user,path,&st)==1&&st.type==GC_FILE&&read(f,path,bytes,64)&&parseOptions(bytes,mask))return mask;}
    return 0;
}
bool saveOptions(gc_fs f,unsigned options,std::string &error){
    error.clear();const std::string path="config/NinMagicGuardian/options.txt",old="config/NinMagicGuardian/options.previous",temp="config/NinMagicGuardian/options.new";gc_stat st{};
    if(options>3||!parents(f,path))return fail(error,"No se pudieron guardar las opciones.");
    for(const auto &p:{path,temp,old}){const int found=f.stat(f.user,p.c_str(),&st);if(found<0||(found&&st.type!=GC_FILE))return fail(error,"Configuración pendiente de revisión.");}
    const int stale=f.stat(f.user,temp.c_str(),&st);if(stale<0||(stale&&f.remove(f.user,temp.c_str(),GC_FILE)))return fail(error,"No se pudo renovar la configuración.");
    const std::string text=std::to_string(options)+"\n";const Bytes bytes(text.begin(),text.end());if(!writeNew(f,temp,bytes,error))return false;
    if(!sync(f,error))return false;
    const int found=f.stat(f.user,path.c_str(),&st);if(found<0||(found&&st.type!=GC_FILE))return fail(error,"Configuración pendiente de revisión.");
    // Do not remove the only valid fallback while preparing its replacement.
    // The new bytes are durable before rotating a present current file.
    if(found){const int prior=f.stat(f.user,old.c_str(),&st);if(prior<0||(prior&&(st.type!=GC_FILE||f.remove(f.user,old.c_str(),GC_FILE)))||f.rename(f.user,path.c_str(),old.c_str())||!sync(f,error))return fail(error,"No se pudieron conservar las opciones anteriores.");}
    if(f.rename(f.user,temp.c_str(),path.c_str())||!sync(f,error))return fail(error,"No se pudieron confirmar las opciones. El respaldo no ha cambiado.");
    return true;
}
}
