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
std::string stopPoint;unsigned stopAt=1,seenPoints=0;
void check(bool value,const char *text){++checks;if(!value)throw std::runtime_error(text);}
#define CHECK(x) check((x),#x)
void put(const fs::path &path,const std::string &value){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out<<value;out.close();CHECK(bool(out));}
std::string content(const fs::path &path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int traceRename(void *user,const char *from,const char *to){
    if(std::find(nmguardian::Targets.begin(),nmguardian::Targets.end(),std::string(to))!=nmguardian::Targets.end())promotions.emplace_back(to);
    if(std::string(to)=="bootloader/ini/ninmagic-guardian.ini"){
        CHECK(content(current/"config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin")=="fixture-rescue");
        CHECK(content(current/"config/NinMagicGuardian/rescue/libsys_minerva.bso")=="fixture-minerva");
    }
    if(to==failDestination)return -1;
    return delegate.rename(user,from,to);
}
int stopCheckpoint(void*,const char *point,const char*){if(stopPoint==point&&++seenPoints==stopAt){stopPoint.clear();return -1;}return 0;}
nmguardian::NativeFs fresh(){
    current=base/("case-"+std::to_string(++cases));fs::create_directory(current);promotions.clear();failDestination.clear();stopPoint.clear();seenPoints=0;stopAt=1;nmguardian::testFailFileSync={};nmguardian::testFailCommit={};return {current.generic_string()};
}
gc_fs traced(nmguardian::NativeFs &native){delegate=native.callbacks();auto io=delegate;io.rename=traceRename;io.checkpoint=stopCheckpoint;return io;}
void legacy(){put(current/"config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin","old rescue");put(current/"config/NinMagicGuardian/rescue/libsys_minerva.bso","old minerva");put(current/"bootloader/ini/ninmagic-guardian.ini","old entry");put(current/"bootloader/hekate_ipl.ini","main untouched");put(current/"atmosphere/package3","system untouched");put(current/"config/NinMagicGuardian/backup/current/manifest.txt","backup untouched");put(current/"config/NinMagicGuardian/addons/inventory.txt","inventory untouched");put(current/"Nintendo/Contents/game","game untouched");}
void verifyScope(){CHECK(content(current/"bootloader/hekate_ipl.ini")=="main untouched"&&content(current/"atmosphere/package3")=="system untouched"&&content(current/"config/NinMagicGuardian/backup/current/manifest.txt")=="backup untouched"&&content(current/"config/NinMagicGuardian/addons/inventory.txt")=="inventory untouched"&&content(current/"Nintendo/Contents/game")=="game untouched");}
void verifyNew(){CHECK(content(current/"config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin")=="fixture-rescue"&&content(current/"config/NinMagicGuardian/rescue/libsys_minerva.bso")=="fixture-minerva"&&content(current/"bootloader/ini/ninmagic-guardian.ini")==content(assets/"rescue/ninmagic-guardian.ini"));}
}
int main(){
    base=fs::temp_directory_path()/("ninmagic-guardian-rescue-test-"+std::to_string(getpid())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        fs::create_directory(base);assets=base/"assets";put(assets/"rescue/ninmagic-guardian-rescue.bin","fixture-rescue");put(assets/"rescue/libsys_minerva.bso","fixture-minerva");put(assets/"rescue/ninmagic-guardian.ini","[Guardian]\npayload=config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin\n");std::string message;
        // First installation commits resources before making the menu entry visible.
        {auto native=fresh();auto io=traced(native);put(current/"bootloader/hekate_ipl.ini","untouched main boot");CHECK(nmguardian::prepareRescue(io,assets.generic_string(),message));CHECK(promotions.size()==3&&promotions.back()=="bootloader/ini/ninmagic-guardian.ini");CHECK(content(current/"bootloader/hekate_ipl.ini")=="untouched main boot");promotions.clear();CHECK(nmguardian::prepareRescue(io,assets.generic_string(),message)&&promotions.empty());}
        // A mismatch at the last destination is detected before any resources
        // can be partially installed or the user's menu entry replaced.
        {auto native=fresh();auto io=traced(native);put(current/"bootloader/ini/ninmagic-guardian.ini/sentinel","user custom entry");CHECK(!nmguardian::prepareRescue(io,assets.generic_string(),message));CHECK(promotions.empty()&&!fs::exists(current/"config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin"));CHECK(content(current/"bootloader/ini/ninmagic-guardian.ini/sentinel")=="user custom entry");}
        {auto native=fresh();auto io=traced(native);put(current/"config/NinMagicGuardian/rescue/libsys_minerva.bso/sentinel","user custom resource");CHECK(!nmguardian::prepareRescue(io,assets.generic_string(),message));CHECK(promotions.empty()&&!fs::exists(current/"bootloader/ini/ninmagic-guardian.ini")&&!fs::exists(current/"config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin"));CHECK(content(current/"config/NinMagicGuardian/rescue/libsys_minerva.bso/sentinel")=="user custom resource");}
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
        // Options retain the original 0..3 format and default to zero.
        {auto native=fresh();auto io=native.callbacks();CHECK(nmguardian::loadOptions(io)==0);for(unsigned options=0;options<=3;++options){CHECK(nmguardian::saveOptions(io,options,message));nmguardian::NativeFs reopened{current.generic_string()};CHECK(nmguardian::loadOptions(reopened.callbacks())==options);CHECK(content(current/"config/NinMagicGuardian/options.txt")==std::to_string(options)+"\n");}CHECK(!nmguardian::saveOptions(io,4,message)&&nmguardian::loadOptions(io)==3);put(current/"config/NinMagicGuardian/options.txt","partial");CHECK(nmguardian::loadOptions(io)==2);}
        // Preflight an invalid current target before removing its fallback.
        {auto native=fresh();auto io=native.callbacks();put(current/"config/NinMagicGuardian/options.previous","2\n");put(current/"sentinel","3\n");CHECK(symlink((current/"sentinel").generic_string().c_str(),(current/"config/NinMagicGuardian/options.txt").generic_string().c_str())==0);CHECK(!nmguardian::saveOptions(io,1,message));CHECK(content(current/"sentinel")=="3\n"&&content(current/"config/NinMagicGuardian/options.previous")=="2\n");CHECK(nmguardian::loadOptions(io)==2);}
        {auto native=fresh();auto io=native.callbacks();put(current/"config/NinMagicGuardian/options.txt","4\n");CHECK(nmguardian::loadOptions(io)==0);}
        // Failure before/during settings rotation preserves the last value.
        {auto native=fresh();auto io=native.callbacks();put(current/"config/NinMagicGuardian/options.previous","0\n");nmguardian::testFailFileSync=[](){return true;};CHECK(!nmguardian::saveOptions(io,3,message));nmguardian::testFailFileSync={};CHECK(nmguardian::loadOptions(io)==0&&content(current/"config/NinMagicGuardian/options.previous")=="0\n");CHECK(nmguardian::saveOptions(io,1,message)&&nmguardian::loadOptions(io)==1);}
        {auto native=fresh();auto io=traced(native);CHECK(nmguardian::saveOptions(io,0,message));failDestination="config/NinMagicGuardian/options.txt";CHECK(!nmguardian::saveOptions(io,3,message));CHECK(nmguardian::loadOptions(io)==0);failDestination.clear();CHECK(nmguardian::saveOptions(io,2,message)&&nmguardian::loadOptions(io)==2);}
        // Upgrade all three OWN targets, retaining immutable previous bytes.
        {auto native=fresh();auto io=traced(native);legacy();CHECK(nmguardian::prepareRescue(io,assets.generic_string(),message));verifyNew();verifyScope();const auto history=current/"config/NinMagicGuardian/rescue-update/generations/00000001";CHECK(content(history/"previous/0.bin")=="old rescue"&&content(history/"previous/1.bin")=="old minerva"&&content(history/"previous/2.bin")=="old entry"&&fs::is_regular_file(history/"completed.txt"));promotions.clear();CHECK(nmguardian::prepareRescue(io,assets.generic_string(),message)&&promotions.empty());}
        // Interrupt at every durable phase, including each member of the pair
        // and the final INI. Repeating the confirmed action must finish safely.
        for(const char *point:{"rescue.staged","rescue.journal","rescue.ready","rescue.retired","rescue.promoted","rescue.complete"})for(unsigned nth=1;nth<=(std::string(point)=="rescue.journal"||std::string(point)=="rescue.complete"?1U:3U);++nth){auto native=fresh();auto io=traced(native);legacy();stopPoint=point;stopAt=nth;CHECK(!nmguardian::prepareRescue(io,assets.generic_string(),message));verifyScope();CHECK(nmguardian::prepareRescue(io,assets.generic_string(),message));verifyNew();verifyScope();CHECK(!fs::exists(current/"config/NinMagicGuardian/rescue-update/active.txt"));}
        // A filesystem commit or writer flush may fail at any stage. Live old
        // files and immutable previous copies must permit a clean retry.
        for(unsigned nth=1;nth<=9;++nth){auto native=fresh();auto io=traced(native);legacy();unsigned seen=0;nmguardian::testFailCommit=[&](){return ++seen==nth;};CHECK(!nmguardian::prepareRescue(io,assets.generic_string(),message));nmguardian::testFailCommit={};CHECK(nmguardian::prepareRescue(io,assets.generic_string(),message));verifyNew();verifyScope();}
        for(unsigned nth=1;nth<=10;++nth){auto native=fresh();auto io=traced(native);legacy();unsigned seen=0;nmguardian::testFailFileSync=[&](){return ++seen==nth;};CHECK(!nmguardian::prepareRescue(io,assets.generic_string(),message));nmguardian::testFailFileSync={};CHECK(nmguardian::prepareRescue(io,assets.generic_string(),message));verifyNew();verifyScope();}
        // A last-destination conflict is preflighted before retiring resource0.
        {auto native=fresh();auto io=traced(native);legacy();stopPoint="rescue.journal";CHECK(!nmguardian::prepareRescue(io,assets.generic_string(),message));put(current/"bootloader/ini/ninmagic-guardian.ini","new external entry");promotions.clear();CHECK(!nmguardian::prepareRescue(io,assets.generic_string(),message)&&promotions.empty());CHECK(content(current/"config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin")=="old rescue"&&content(current/"bootloader/ini/ninmagic-guardian.ini")=="new external entry");verifyScope();}
        // Corrupt staged bytes or the sealed journal never authorize a write.
        for(bool journal:{false,true}){auto native=fresh();auto io=traced(native);legacy();stopPoint="rescue.journal";CHECK(!nmguardian::prepareRescue(io,assets.generic_string(),message));put(current/(journal?"config/NinMagicGuardian/rescue-update/active.txt":"config/NinMagicGuardian/rescue-update/generations/00000001/incoming/1.bin"),"changed");promotions.clear();CHECK(!nmguardian::prepareRescue(io,assets.generic_string(),message)&&promotions.empty());CHECK(content(current/"config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin")=="old rescue");verifyScope();}
        // A regular incomplete OWN temporary is recoverable; directories/links
        // at the same path are refused rather than removed.
        {auto native=fresh();auto io=traced(native);legacy();stopPoint="rescue.journal";CHECK(!nmguardian::prepareRescue(io,assets.generic_string(),message));put(current/"config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin.guardian-new-00000001","partial");CHECK(nmguardian::prepareRescue(io,assets.generic_string(),message));verifyNew();verifyScope();}
        for(unsigned target:{0U,2U})for(bool link:{false,true}){auto native=fresh();auto io=traced(native);legacy();stopPoint="rescue.journal";CHECK(!nmguardian::prepareRescue(io,assets.generic_string(),message));const auto temp=current/(std::string(nmguardian::Targets[target])+".guardian-new-00000001");if(link){put(current/"sentinel","untouched");CHECK(symlink((current/"sentinel").generic_string().c_str(),temp.generic_string().c_str())==0);}else fs::create_directory(temp);promotions.clear();CHECK(!nmguardian::prepareRescue(io,assets.generic_string(),message)&&promotions.empty());CHECK(content(current/"config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin")=="old rescue");if(link)CHECK(content(current/"sentinel")=="untouched");verifyScope();}
        // A changed previous/retired file blocks a pending generation without
        // discarding evidence or restoring an older resource over the new one.
        for(bool retired:{false,true}){auto native=fresh();auto io=traced(native);legacy();stopPoint=retired?"rescue.retired":"rescue.journal";CHECK(!nmguardian::prepareRescue(io,assets.generic_string(),message));put(current/(retired?"config/NinMagicGuardian/rescue-update/generations/00000001/retired/0.bin":"config/NinMagicGuardian/rescue-update/generations/00000001/previous/0.bin"),"changed");promotions.clear();CHECK(!nmguardian::prepareRescue(io,assets.generic_string(),message)&&promotions.empty());CHECK(fs::exists(current/"config/NinMagicGuardian/rescue-update/active.txt"));verifyScope();}
        const auto checked=fs::weakly_canonical(base);CHECK(checked.parent_path()==fs::weakly_canonical(fs::temp_directory_path())&&checked.filename().string().find("ninmagic-guardian-rescue-test-")==0);fs::remove_all(checked);std::cout<<"Guardian rescue preparation/options: "<<cases<<" scenarios / "<<checks<<" checks PASS\n";return 0;
    }catch(const std::exception &error){std::cerr<<"FAIL case "<<cases<<": "<<error.what()<<"\nFixture retained: "<<base<<'\n';return 1;}
}
