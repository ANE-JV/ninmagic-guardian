#define NM_GUARDIAN_NATIVE_TEST
#include "../source/guardian_native.cpp"
#include "../source/rescue_install.cpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
namespace {
unsigned checks=0,cases=0;
fs::path base,current,assets;
gc_fs delegate{};
std::vector<std::string> promotions;
std::string failDestination;
void check(bool value,const char *text){++checks;if(!value)throw std::runtime_error(text);}
#define CHECK(x) check((x),#x)
void put(const fs::path &path,const std::string &value){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out<<value;out.close();CHECK(bool(out));}
std::string content(const fs::path &path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int traceRename(void *user,const char *from,const char *to){
    promotions.emplace_back(to);
    if(std::string(to)=="bootloader/ini/ninmagic-guardian.ini"){
        CHECK(content(current/"config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin")=="fixture-rescue");
        CHECK(content(current/"config/NinMagicGuardian/rescue/libsys_minerva.bso")=="fixture-minerva");
    }
    if(to==failDestination)return -1;
    return delegate.rename(user,from,to);
}
nmguardian::NativeFs fresh(){
    current=base/("case-"+std::to_string(++cases));fs::create_directory(current);promotions.clear();failDestination.clear();nmguardian::testFailFileSync={};nmguardian::testFailCommit={};return {current.generic_string()};
}
gc_fs traced(nmguardian::NativeFs &native){delegate=native.callbacks();auto io=delegate;io.rename=traceRename;return io;}
}
int main(){
    base=fs::temp_directory_path()/("ninmagic-guardian-rescue-test-"+std::to_string(getpid())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        fs::create_directory(base);assets=base/"assets";put(assets/"rescue/ninmagic-guardian-rescue.bin","fixture-rescue");put(assets/"rescue/libsys_minerva.bso","fixture-minerva");put(assets/"rescue/ninmagic-guardian.ini","[Guardian]\npayload=config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin\n");std::string message;
        // First installation commits resources before making the menu entry visible.
        {auto native=fresh();auto io=traced(native);put(current/"bootloader/hekate_ipl.ini","untouched main boot");CHECK(nmguardian::prepareRescue(io,assets.generic_string(),message));CHECK(promotions.size()==3&&promotions.back()=="bootloader/ini/ninmagic-guardian.ini");CHECK(content(current/"bootloader/hekate_ipl.ini")=="untouched main boot");promotions.clear();CHECK(nmguardian::prepareRescue(io,assets.generic_string(),message)&&promotions.empty());}
        // A mismatch at the last destination is detected before any resources
        // can be partially installed or the user's menu entry replaced.
        {auto native=fresh();auto io=traced(native);put(current/"bootloader/ini/ninmagic-guardian.ini","user custom entry");CHECK(!nmguardian::prepareRescue(io,assets.generic_string(),message));CHECK(promotions.empty()&&!fs::exists(current/"config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin"));CHECK(content(current/"bootloader/ini/ninmagic-guardian.ini")=="user custom entry");}
        {auto native=fresh();auto io=traced(native);put(current/"config/NinMagicGuardian/rescue/libsys_minerva.bso","user custom resource");CHECK(!nmguardian::prepareRescue(io,assets.generic_string(),message));CHECK(promotions.empty()&&!fs::exists(current/"bootloader/ini/ninmagic-guardian.ini")&&!fs::exists(current/"config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin"));CHECK(content(current/"config/NinMagicGuardian/rescue/libsys_minerva.bso")=="user custom resource");}
        // Fail promoting resource two: no Hekate entry can point at an
        // incomplete rescue. Retrying reuses the first verified resource.
        {auto native=fresh();auto io=traced(native);failDestination="config/NinMagicGuardian/rescue/libsys_minerva.bso";CHECK(!nmguardian::prepareRescue(io,assets.generic_string(),message));CHECK(content(current/"config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin")=="fixture-rescue");CHECK(!fs::exists(current/"bootloader/ini/ninmagic-guardian.ini"));failDestination.clear();promotions.clear();CHECK(nmguardian::prepareRescue(io,assets.generic_string(),message));CHECK(promotions.size()==2&&promotions.back()=="bootloader/ini/ninmagic-guardian.ini");}
        // An absent embedded asset is a read-only failure.
        {auto native=fresh();auto io=traced(native);CHECK(!nmguardian::prepareRescue(io,(base/"missing-assets").generic_string(),message));CHECK(fs::is_empty(current)&&promotions.empty());}
        // Leaf and parent links never become install targets.
        {auto native=fresh();auto io=traced(native);put(current/"sentinel","outside content");fs::create_directories(current/"config/NinMagicGuardian/rescue");CHECK(symlink((current/"sentinel").generic_string().c_str(),(current/"config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin").generic_string().c_str())==0);CHECK(!nmguardian::prepareRescue(io,assets.generic_string(),message));CHECK(content(current/"sentinel")=="outside content"&&promotions.empty());}
        {auto native=fresh();auto io=traced(native);fs::create_directory(current/"outside");CHECK(symlink((current/"outside").generic_string().c_str(),(current/"config").generic_string().c_str())==0);CHECK(!nmguardian::prepareRescue(io,assets.generic_string(),message));CHECK(fs::is_empty(current/"outside")&&promotions.empty());}
        // A write flush failure cannot install a menu entry.
        {auto native=fresh();auto io=traced(native);nmguardian::testFailFileSync=[](){return true;};CHECK(!nmguardian::prepareRescue(io,assets.generic_string(),message));nmguardian::testFailFileSync={};CHECK(!fs::exists(current/"bootloader/ini/ninmagic-guardian.ini")&&promotions.empty());}
        // Options survive object/process-style recreation, with the prior file
        // acting as fallback if a final promotion was interrupted.
        {auto native=fresh();auto io=native.callbacks();CHECK(nmguardian::loadOptions(io)==0);for(unsigned options=0;options<=3;++options){CHECK(nmguardian::saveOptions(io,options,message));nmguardian::NativeFs reopened{current.generic_string()};CHECK(nmguardian::loadOptions(reopened.callbacks())==options);}CHECK(!nmguardian::saveOptions(io,4,message)&&nmguardian::loadOptions(io)==3);put(current/"config/NinMagicGuardian/options.txt","partial");CHECK(nmguardian::loadOptions(io)==2);}
        // Preflight an invalid current target before removing its valid previous
        // fallback; neither the link's target nor the fallback may change.
        {auto native=fresh();auto io=native.callbacks();put(current/"config/NinMagicGuardian/options.previous","2\n");put(current/"sentinel","3\n");CHECK(symlink((current/"sentinel").generic_string().c_str(),(current/"config/NinMagicGuardian/options.txt").generic_string().c_str())==0);CHECK(!nmguardian::saveOptions(io,1,message));CHECK(content(current/"sentinel")=="3\n"&&content(current/"config/NinMagicGuardian/options.previous")=="2\n");CHECK(nmguardian::loadOptions(io)==2);}
        const auto checked=fs::weakly_canonical(base);CHECK(checked.parent_path()==fs::weakly_canonical(fs::temp_directory_path())&&checked.filename().string().find("ninmagic-guardian-rescue-test-")==0);fs::remove_all(checked);std::cout<<"Guardian rescue preparation/options: "<<cases<<" scenarios / "<<checks<<" checks PASS\n";return 0;
    }catch(const std::exception &error){std::cerr<<"FAIL case "<<cases<<": "<<error.what()<<"\nFixture retained: "<<base<<'\n';return 1;}
}
