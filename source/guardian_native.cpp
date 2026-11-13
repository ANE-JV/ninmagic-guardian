#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "guardian_native.hpp"
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#ifdef __SWITCH__
#include <switch.h>
#elif defined(__CYGWIN__)
#define NOMINMAX
#include <windows.h>
#include <sys/cygwin.h>
#elif defined(__linux__)
#include <sys/syscall.h>
#include <linux/fs.h>
#endif
#ifdef NM_GUARDIAN_NATIVE_TEST
#include <functional>
#endif

namespace nmguardian {
namespace {
#ifdef NM_GUARDIAN_NATIVE_TEST
std::function<bool()> testFailFileSync,testFailCommit;
#endif
enum class PathState { Present,Missing,Error };
struct NativeFile { FILE *stream;bool writer; };
gc_type kind(const struct stat &st){return S_ISREG(st.st_mode)?GC_FILE:S_ISDIR(st.st_mode)?GC_DIR:GC_OTHER;}
bool safeRelative(const char *path){
    if(!path)return false;
    const size_t length=std::strlen(path);if(!length||length>=512)return false;
    if(length<=GC_PATH_MAX)return gc_safe_path(path)!=0;
    // Internal backup paths prefix the already validated managed SD path.
    // Validate every segment with the same policy without truncating that path.
    std::string value(path);size_t begin=0;
    for(;;){const auto end=value.find('/',begin);if(!gc_safe_path(value.substr(begin,end==std::string::npos?end:end-begin).c_str()))return false;if(end==std::string::npos)return true;begin=end+1;}
}
bool validRoot(const std::string &root){
    if(root.empty())return false;
#ifdef __SWITCH__
    if(root!="sdmc:")return false;
    struct stat st{};return lstat("sdmc:/",&st)==0&&S_ISDIR(st.st_mode)&&!S_ISLNK(st.st_mode);
#else
    if(root.find('\\')!=std::string::npos)return false;
    const bool drive=root.size()>=3&&root[1]==':'&&root[2]=='/';if(root.front()!='/'&&!drive)return false;
    size_t start=drive?3:1;struct stat st{};const auto base=root.substr(0,start);
    if(lstat(base.c_str(),&st)!=0||!S_ISDIR(st.st_mode)||S_ISLNK(st.st_mode))return false;
    for(;;){const auto slash=root.find('/',start);const auto component=root.substr(0,slash);if(lstat(component.c_str(),&st)!=0||!S_ISDIR(st.st_mode)||S_ISLNK(st.st_mode))return false;if(slash==std::string::npos)break;start=slash+1;if(start==root.size())break;}
    return true;
#endif
}
// lstat every existing component. An absent intermediate can be reported as
// missing by stat, but cannot be used as the parent of an open/mkdir/rename.
PathState resolve(NativeFs &fs,const char *relative,std::string &path,struct stat *out=nullptr,bool leafMayBeMissing=false){
    path.clear();if(!safeRelative(relative)||!validRoot(fs.root))return PathState::Error;
    path=fs.root;while(path.size()>1&&path.back()=='/')path.pop_back();
    struct stat st{};
    std::string rel(relative);size_t offset=0;
    while(offset<rel.size()){
        const auto end=rel.find('/',offset);const bool last=end==std::string::npos;
        path+="/"+rel.substr(offset,last?std::string::npos:end-offset);
        if(lstat(path.c_str(),&st)!=0){
            if(errno==ENOENT&&last&&leafMayBeMissing)return PathState::Missing;
            return errno==ENOENT?PathState::Missing:PathState::Error;
        }
        if(S_ISLNK(st.st_mode)||(!last&&!S_ISDIR(st.st_mode)))return PathState::Error;
        if(last){if(out)*out=st;return PathState::Present;}
        offset=end+1;
    }
    return PathState::Error;
}
bool creationPath(NativeFs &fs,const char *rel,std::string &path){
    if(!safeRelative(rel))return false;
    const std::string relative(rel);const auto slash=relative.rfind('/');
    if(slash!=std::string::npos){std::string parent;struct stat st{};if(resolve(fs,relative.substr(0,slash).c_str(),parent,&st)!=PathState::Present||!S_ISDIR(st.st_mode))return false;}
    return resolve(fs,rel,path,nullptr,true)==PathState::Missing&&path==fs.root+(fs.root.back()=='/'?"":"/")+relative;
}
int nativeStat(void *user,const char *path,gc_stat *result){
    if(!user||!result)return -1;
    std::string native;struct stat st{};auto state=resolve(*static_cast<NativeFs*>(user),path,native,&st);
    if(state==PathState::Missing){*result={GC_MISSING,0};return 0;}
    if(state!=PathState::Present||st.st_size<0)return -1;
    *result={kind(st),static_cast<uint64_t>(st.st_size)};return 1;
}
int nativeList(void *user,const char *path,gc_visit visit,void *visitUser){
    if(!user||!visit)return -1;
    auto &fs=*static_cast<NativeFs*>(user);std::string native;struct stat st{};
    if(resolve(fs,path,native,&st)!=PathState::Present||!S_ISDIR(st.st_mode))return -1;
    DIR *dir=opendir(native.c_str());if(!dir)return -1;bool ok=true;
    for(;;){errno=0;auto *entry=readdir(dir);if(!entry){if(errno)ok=false;break;}if(!std::strcmp(entry->d_name,".")||!std::strcmp(entry->d_name,".."))continue;
        const std::string relative=std::string(path)+"/"+entry->d_name;std::string resolved;struct stat item{};
        if(resolve(fs,relative.c_str(),resolved,&item)!=PathState::Present||visit(visitUser,entry->d_name,kind(item))!=0){ok=false;break;}
    }
    if(closedir(dir)!=0)ok=false;
    return ok?0:-1;
}
void *nativeOpen(void *user,const char *path,char mode){
    if(!user||(mode!='r'&&mode!='x'))return nullptr;
    auto &fs=*static_cast<NativeFs*>(user);std::string native;struct stat st{};
    if(mode=='r'){if(resolve(fs,path,native,&st)!=PathState::Present||!S_ISREG(st.st_mode))return nullptr;}
    else if(!creationPath(fs,path,native))return nullptr;
    int flags=mode=='x'?O_WRONLY|O_CREAT|O_EXCL:O_RDONLY;
#ifdef O_NOFOLLOW
    flags|=O_NOFOLLOW;
#endif
    int fd=open(native.c_str(),flags,0600);if(fd<0)return nullptr;
    if(fstat(fd,&st)!=0||!S_ISREG(st.st_mode)){close(fd);return nullptr;}
    FILE *stream=fdopen(fd,mode=='x'?"wb":"rb");if(!stream){close(fd);return nullptr;}
    auto *handle=new(std::nothrow) NativeFile{stream,mode=='x'};if(!handle)fclose(stream);return handle;
}
int nativeRead(void*,void *file,void *bytes,size_t size,size_t *count){
    if(!file||!count||(!bytes&&size))return -1;
    auto &h=*static_cast<NativeFile*>(file);if(h.writer)return -1;
    *count=fread(bytes,1,size,h.stream);return ferror(h.stream)?-1:0;
}
int nativeWrite(void*,void *file,const void *bytes,size_t size){
    if(!file||(!bytes&&size))return -1;
    auto &h=*static_cast<NativeFile*>(file);return h.writer&&fwrite(bytes,1,size,h.stream)==size&&!ferror(h.stream)?0:-1;
}
int nativeFileSync(void*,void *file){
    if(!file)return -1;
    auto &h=*static_cast<NativeFile*>(file);if(!h.writer)return -1;
#ifdef NM_GUARDIAN_NATIVE_TEST
    if(testFailFileSync&&testFailFileSync())return -1;
#endif
    return fflush(h.stream)==0&&fsync(fileno(h.stream))==0?0:-1;
}
int nativeClose(void*,void *file){if(!file)return -1;auto *h=static_cast<NativeFile*>(file);const int result=fclose(h->stream);delete h;return result==0?0:-1;}
int nativeMkdir(void *user,const char *path){
    if(!user)return -1;
    auto &fs=*static_cast<NativeFs*>(user);std::string native;struct stat st{};
    if(resolve(fs,path,native,&st)==PathState::Present)return S_ISDIR(st.st_mode)?0:-1;
    if(!creationPath(fs,path,native))return -1;
    return mkdir(native.c_str(),0700)==0?0:-1;
}
int nativeRename(void *user,const char *from,const char *to){
    if(!user)return -1;
    auto &fs=*static_cast<NativeFs*>(user);std::string source,destination;struct stat st{};
    if(resolve(fs,from,source,&st)!=PathState::Present||(kind(st)!=GC_FILE&&kind(st)!=GC_DIR)||!creationPath(fs,to,destination))return -1;
#ifdef __SWITCH__
    char sourcePath[FS_MAX_PATH]{},destinationPath[FS_MAX_PATH]{};FsFileSystem *sourceFs=nullptr,*destinationFs=nullptr;
    if(fsdevTranslatePath(source.c_str(),&sourceFs,sourcePath)!=0||fsdevTranslatePath(destination.c_str(),&destinationFs,destinationPath)!=0||!sourceFs||sourceFs!=destinationFs)return -1;
    return R_SUCCEEDED(S_ISDIR(st.st_mode)?fsFsRenameDirectory(sourceFs,sourcePath,destinationPath):fsFsRenameFile(sourceFs,sourcePath,destinationPath))?0:-1;
#elif defined(__CYGWIN__)
    std::array<wchar_t,32768> a{},b{};
    if(cygwin_conv_path(CCP_POSIX_TO_WIN_W,source.c_str(),a.data(),sizeof(a))!=0||cygwin_conv_path(CCP_POSIX_TO_WIN_W,destination.c_str(),b.data(),sizeof(b))!=0)return -1;
    return MoveFileExW(a.data(),b.data(),MOVEFILE_WRITE_THROUGH)?0:-1;
#elif defined(__linux__) && defined(SYS_renameat2)
    return syscall(SYS_renameat2,AT_FDCWD,source.c_str(),AT_FDCWD,destination.c_str(),RENAME_NOREPLACE)==0?0:-1;
#else
    // An overwrite-capable fallback would violate the journal's contract.
    return -1;
#endif
}
int nativeRemove(void *user,const char *path,gc_type type){
    if(!user||(type!=GC_FILE&&type!=GC_DIR))return -1;
    std::string native;struct stat st{};
    if(resolve(*static_cast<NativeFs*>(user),path,native,&st)!=PathState::Present||kind(st)!=type)return -1;
    return (type==GC_DIR?rmdir(native.c_str()):unlink(native.c_str()))==0?0:-1;
}
int nativeSync(void *user){
    if(!user)return -1;
#ifdef NM_GUARDIAN_NATIVE_TEST
    if(testFailCommit&&testFailCommit())return -1;
#endif
    auto &fs=*static_cast<NativeFs*>(user);if(!validRoot(fs.root))return -1;
#ifdef __SWITCH__
    // Pass the mount name without ':'; libnx resolves that exact device.
    return fs.root=="sdmc:"&&R_SUCCEEDED(fsdevCommitDevice("sdmc"))?0:-1;
#else
    int flags=O_RDONLY;
#ifdef O_DIRECTORY
    flags|=O_DIRECTORY;
#endif
    const int fd=open(fs.root.c_str(),flags);if(fd<0)return -1;const bool ok=fsync(fd)==0;const int closed=close(fd);return ok&&closed==0?0:-1;
#endif
}
int nativeFreeSpace(void *user,uint64_t *bytes){
    if(!user||!bytes)return -1;
    auto &fs=*static_cast<NativeFs*>(user);if(!validRoot(fs.root))return -1;
    struct statvfs info{};
    const std::string root=fs.root+"/";if(statvfs(root.c_str(),&info)!=0)return -1;
    const uint64_t block=info.f_frsize?info.f_frsize:info.f_bsize;if(block&&uint64_t(info.f_bavail)>std::numeric_limits<uint64_t>::max()/block)return -1;*bytes=uint64_t(info.f_bavail)*block;return 0;
}
}
gc_fs NativeFs::callbacks(){gc_fs out{};out.user=this;out.stat=nativeStat;out.list=nativeList;out.open=nativeOpen;out.read=nativeRead;out.write=nativeWrite;out.file_sync=nativeFileSync;out.close=nativeClose;out.mkdir=nativeMkdir;out.rename=nativeRename;out.remove=nativeRemove;out.sync=nativeSync;out.free_space=nativeFreeSpace;return out;}
Runtime inspectRuntime(){
    Runtime runtime;runtime.firmware="Sin comprobar";runtime.model="Sin comprobar";
#ifdef __SWITCH__
    u64 atmosphereVersion=0,hardware=UINT64_MAX;Result spl=splInitialize();
    if(R_SUCCEEDED(spl)){runtime.atmosphere=R_SUCCEEDED(splGetConfig(static_cast<SplConfigItem>(65000),&atmosphereVersion));splGetConfig(SplConfigItem_HardwareType,&hardware);splExit();}
    // Atmosphere spl_types.hpp: Icosa=0, Hoag=2, Iowa=3, Aula=5.
    runtime.model=hardware==0?"Switch V1":hardware==2?"Switch Lite":hardware==3?"Switch V2":hardware==5?"Switch OLED":"Nintendo Switch";
    if(R_SUCCEEDED(setsysInitialize())){SetSysFirmwareVersion version{};if(R_SUCCEEDED(setsysGetFirmwareVersion(&version)))runtime.firmware=std::to_string(version.major)+"."+std::to_string(version.minor)+"."+std::to_string(version.micro);setsysExit();}
    FsDeviceOperator device{};const Result opened=fsOpenDeviceOperator(&device);Result cidResult=opened;
    std::array<unsigned char,16> cid{};
    if(R_SUCCEEDED(opened)){cidResult=fsDeviceOperatorGetSdCardCid(&device,cid.data(),cid.size(),cid.size());fsDeviceOperatorClose(&device);}
    bool any=false;for(auto value:cid)any=any||value!=0;
    if(R_SUCCEEDED(cidResult)&&any){constexpr char hex[]="0123456789abcdef";for(auto value:cid){runtime.identity+=hex[value>>4];runtime.identity+=hex[value&15];}runtime.identityValid=true;}
    if(!runtime.atmosphere)runtime.detail="No se pudo confirmar Atmosphère. Las escrituras están bloqueadas.";
    else if(!runtime.identityValid)runtime.detail="No se pudo leer la identidad física de esta microSD. No se escribirá ningún respaldo.";
    else runtime.detail="Atmosphère y microSD identificados. Guardian solo trabaja con archivos de la SD.";
#else
    runtime.detail="La identidad física de la microSD se comprueba en la consola; este entorno es de pruebas.";
#endif
    return runtime;
}
}
