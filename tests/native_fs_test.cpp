#define NM_GUARDIAN_NATIVE_TEST
#include "../source/guardian_native.cpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace fs=std::filesystem;
namespace {
unsigned checks=0;
void check(bool ok,const char *text){++checks;if(!ok)throw std::runtime_error(text);}
#define CHECK(x) check((x),#x)
void put(const fs::path &path,const std::string &text){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out<<text;out.close();CHECK(bool(out));}
std::string read(const fs::path &path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int collect(void *user,const char *name,gc_type type){CHECK(type==GC_FILE||type==GC_DIR);static_cast<std::vector<std::string>*>(user)->push_back(name);return 0;}
int stop(void*,const char*,gc_type){return -1;}
}
int main(){
    const fs::path fixture=fs::temp_directory_path()/("ninmagic-guardian-native-test-"+std::to_string(getpid())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        fs::create_directory(fixture);nmguardian::NativeFs native{fixture.generic_string()};auto io=native.callbacks();gc_stat entry{};
        CHECK(io.stat(io.user,"missing",&entry)==0&&entry.type==GC_MISSING);
        CHECK(io.stat(io.user,"missing/child",&entry)==0);
        CHECK(io.mkdir(io.user,"work")==0&&io.mkdir(io.user,"work")==0);
        CHECK(io.mkdir(io.user,"missing/child")==-1);
        for(const std::string &path:std::vector<std::string>{"",".","..","../outside","work/../outside","/outside","work//x","work/","C:/outside","work\\x","work/*","work/\nfile",std::string(512,'x')}){
            CHECK(io.stat(io.user,path.c_str(),&entry)==-1);
            CHECK(io.open(io.user,path.c_str(),'x')==nullptr);
            CHECK(io.mkdir(io.user,path.c_str())==-1);
        }
        void *writer=io.open(io.user,"work/data",'x');CHECK(writer);
        CHECK(io.write(io.user,writer,"abc",3)==0);
        size_t count=0;char bytes[8]{};CHECK(io.read(io.user,writer,bytes,sizeof bytes,&count)==-1);
        nmguardian::testFailFileSync=[](){return true;};CHECK(io.file_sync(io.user,writer)==-1);nmguardian::testFailFileSync={};
        CHECK(io.file_sync(io.user,writer)==0&&io.close(io.user,writer)==0);
        CHECK(io.open(io.user,"work/data",'x')==nullptr&&read(fixture/"work/data")=="abc");
        CHECK(io.stat(io.user,"work/data",&entry)==1&&entry.type==GC_FILE&&entry.size==3);
        CHECK(io.mkdir(io.user,"work/data")==-1&&io.open(io.user,"work",'r')==nullptr);
        void *reader=io.open(io.user,"work/data",'r');CHECK(reader);
        CHECK(io.read(io.user,reader,bytes,sizeof bytes,&count)==0&&count==3&&std::string(bytes,count)=="abc");
        CHECK(io.write(io.user,reader,"x",1)==-1&&io.file_sync(io.user,reader)==-1);
        CHECK(io.read(io.user,reader,bytes,sizeof bytes,&count)==0&&count==0&&io.close(io.user,reader)==0);
        std::vector<std::string> names;CHECK(io.list(io.user,"work",collect,&names)==0&&names.size()==1&&names.front()=="data");
        CHECK(io.list(io.user,"work",stop,nullptr)==-1&&io.list(io.user,"work/data",collect,&names)==-1);
        put(fixture/"work/occupied","sentinel");CHECK(io.rename(io.user,"work/data","work/occupied")==-1);
        CHECK(read(fixture/"work/data")=="abc"&&read(fixture/"work/occupied")=="sentinel");
        CHECK(io.rename(io.user,"work/data","work/moved")==0&&!fs::exists(fixture/"work/data")&&read(fixture/"work/moved")=="abc");
        CHECK(io.mkdir(io.user,"tree")==0&&io.mkdir(io.user,"occupied-tree")==0);
        put(fixture/"tree/inside","inside");CHECK(io.rename(io.user,"tree","occupied-tree")==-1);
        CHECK(io.rename(io.user,"tree","moved-tree")==0&&read(fixture/"moved-tree/inside")=="inside");
        CHECK(io.remove(io.user,"work/occupied",GC_DIR)==-1&&io.remove(io.user,"work",GC_DIR)==-1);
        CHECK(io.remove(io.user,"work/occupied",GC_FILE)==0&&!fs::exists(fixture/"work/occupied"));
        CHECK(io.remove(io.user,"moved-tree/inside",GC_FILE)==0&&io.remove(io.user,"moved-tree",GC_DIR)==0);
        uint64_t available=0;CHECK(io.free_space(io.user,&available)==0&&available>0);
        nmguardian::testFailCommit=[](){return true;};CHECK(io.sync(io.user)==-1);nmguardian::testFailCommit={};CHECK(io.sync(io.user)==0);
        // Backup data prefixes can legitimately push an internal path past the
        // managed-file limit, without allowing long unsafe components/traversal.
        const std::string longPath="work/"+std::string(100,'a')+"/"+std::string(100,'b')+"/"+std::string(50,'c')+".bin";
        fs::create_directories((fixture/longPath).parent_path());CHECK(longPath.size()>GC_PATH_MAX);
        writer=io.open(io.user,longPath.c_str(),'x');CHECK(writer&&io.write(io.user,writer,"long",4)==0&&io.file_sync(io.user,writer)==0&&io.close(io.user,writer)==0);
        CHECK(io.stat(io.user,longPath.c_str(),&entry)==1&&entry.size==4);
        CHECK(io.rename(io.user,longPath.c_str(),(longPath+".moved").c_str())==0&&read(fixture/(longPath+".moved"))=="long");
        // Both leaf and intermediate links must be refused, including an SD-root
        // alias, and the target remains intact after every attempted mutation.
        CHECK(symlink((fixture/"work/moved").generic_string().c_str(),(fixture/"link-file").generic_string().c_str())==0);
        CHECK(symlink((fixture/"work").generic_string().c_str(),(fixture/"link-dir").generic_string().c_str())==0);
        for(const char *path:{"link-file","link-dir","link-dir/moved","link-dir/new"}){
            CHECK(io.stat(io.user,path,&entry)==-1&&io.open(io.user,path,'r')==nullptr&&io.open(io.user,path,'x')==nullptr);
            CHECK(io.mkdir(io.user,path)==-1&&io.remove(io.user,path,GC_FILE)==-1&&io.rename(io.user,path,"escape")==-1);
        }
        CHECK(io.rename(io.user,"work/moved","link-dir/new")==-1&&read(fixture/"work/moved")=="abc");
        nmguardian::NativeFs aliased{(fixture/"link-dir").generic_string()};auto alias=aliased.callbacks();CHECK(alias.stat(alias.user,"moved",&entry)==-1&&alias.sync(alias.user)==-1);
        CHECK(io.list(io.user,"work",collect,&names)==0);
        CHECK(symlink((fixture/"work/moved").generic_string().c_str(),(fixture/"work/linked").generic_string().c_str())==0);
        CHECK(io.list(io.user,"work",collect,&names)==-1&&read(fixture/"work/moved")=="abc");
        CHECK(unlink((fixture/"work/linked").generic_string().c_str())==0);
        // Real engine integration: native renames and commit semantics must
        // preserve games added after a verified backup.
        put(fixture/"atmosphere/package3","working system");put(fixture/"bootloader/hekate_ipl.ini","[CFW]\nfss0=atmosphere/package3\n");
        gc_context context{};context.fs=io;std::strcpy(context.identity,"0102030405060708090a0b0c0d0e0f10");std::strcpy(context.firmware,"23.0.1");std::strcpy(context.model,"fixture");std::strcpy(context.created,"2026-11-13");
        auto backup=gc_create_backup(&context,0);if(!backup.ok)std::cerr<<backup.message<<'\n';CHECK(backup.ok);
        auto inspected=gc_inspect(&context,1);CHECK(inspected.ok&&inspected.info.verified);
        put(fixture/"Nintendo/Contents/new-game","installed after backup");put(fixture/"atmosphere/package3","damaged system");
        auto restored=gc_restore(&context);if(!restored.ok)std::cerr<<restored.message<<'\n';CHECK(restored.ok);
        CHECK(read(fixture/"atmosphere/package3")=="working system"&&read(fixture/"Nintendo/Contents/new-game")=="installed after backup");
        // A flush failure cannot promote a new backup over the verified one.
        nmguardian::testFailFileSync=[](){return true;};backup=gc_create_backup(&context,0);nmguardian::testFailFileSync={};CHECK(!backup.ok);
        inspected=gc_inspect(&context,1);CHECK(inspected.ok&&inspected.info.verified);
        auto runtime=nmguardian::inspectRuntime();CHECK(!runtime.identityValid&&!runtime.atmosphere&&runtime.identity.empty());
        const auto checked=fs::weakly_canonical(fixture);CHECK(checked.parent_path()==fs::weakly_canonical(fs::temp_directory_path())&&checked.filename().string().find("ninmagic-guardian-native-test-")==0);fs::remove_all(checked);
        std::cout<<"Guardian native SD adapter: "<<checks<<" checks PASS\n";return 0;
    }catch(const std::exception &error){std::cerr<<"FAIL: "<<error.what()<<"\nFixture retained: "<<fixture<<'\n';return 1;}
}
