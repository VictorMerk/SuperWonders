#include "profiles.h"
#include "user_data_lock.h"
#include "launcher/save_backup.h"
#include "overlay/usability.h"
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <fstream>
static void check(bool b,const char* text){if(!b){std::fprintf(stderr,"%s\n",text);std::exit(1);}}
static std::string read(const std::filesystem::path& p){std::ifstream f(p);std::string s;std::getline(f,s);return s;}
int main(){
 namespace fs=std::filesystem;
 const auto base=fs::temp_directory_path();
 const auto data=base/("wonder-profiles-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 fs::create_directories(data/"user/save/slot");
 {std::ofstream f(data/"user/save/slot/progress");f<<"original";}
 std::string error;
#if defined(_WIN32)
 {nx::UserDataLock first,second;
 check(first.acquire(data,error),"first instance locks save data");
 check(!second.acquire(data,error),"second instance cannot modify live save data");}
 {nx::UserDataLock next;check(next.acquire(data,error),"lock released after owner closes");}
#endif
 check(nx::profiles::active(data).id=="default","legacy default selected");
 check(nx::profiles::user_root(data)==data/"user","legacy save path retained");
 check(nx::profiles::rename(data,"Victor",error),"rename default");
 check(nx::profiles::nickname(data)=="Victor","nickname persisted");
 const auto backup=nx::launcher::backup_saves(data,error);
 check(!backup.empty(),"original backup");
 {std::ofstream f(data/"user/save/slot/progress");f<<"new progress";}
 check(nx::launcher::restore_saves(data,backup,error),"restore succeeds");
 check(read(data/"user/save/slot/progress")=="original","restored content");
 bool preserved=false;
 for(const auto& b:nx::launcher::list_save_backups(data))if(read(b/"user/save/slot/progress")=="new progress")preserved=true;
 check(preserved,"pre-restore progress preserved");
 check(!nx::launcher::restore_saves(data,data/"user",error),"outside backup rejected");
 fs::create_directories(data/"backups/incomplete.partial/user/save");
 check(!nx::launcher::restore_saves(data,data/"backups/incomplete.partial",error),"incomplete backup rejected");
 check(read(data/"user/save/slot/progress")=="original","failed restore leaves current saves unchanged");
 check(nx::profiles::create(data,"Brother",error),"create separate profile");
 const auto other=nx::profiles::storage_root(data);
 check(other!=data && !fs::exists(other/"user/save/slot/progress"),"fresh saves isolated");
 check(nx::profiles::nickname(data)=="Brother","selected name");
 check(read(data/"user/save/slot/progress")=="original","original save unchanged");
 check(!nx::profiles::select(data,"../../bad",error),"path traversal rejected");
 check(!nx::profiles::rename(data,"bad\nname",error),"control character rejected");
 check(!nx::profiles::rename(data,std::string(32,'x'),error),"oversize nickname rejected");
 check(!nx::profiles::rename(data,std::string("\xc0\xaf",2),error),"invalid UTF-8 rejected");
 check(nx::profiles::rename(data,"J\xc3\xb6rg",error),"UTF-8 name accepted");
 check(nx::profiles::select(data,"default",error),"switch back");
 check(nx::profiles::nickname(data)=="Victor","names independent");
 {std::ofstream f(data/"active-profile.txt");f<<"../../outside";}
 check(nx::profiles::user_root(data)==data/"user","corrupt selection falls back safely");
 nx::overlay::Usability u;u.auto_backup=false;u.reduced_motion=true;u.menu_opacity=85;u.menu_fullscreen=true;
 check(nx::overlay::save_usability(data,u),"save usability");
 const auto v=nx::overlay::load_usability(data);
 check(!v.auto_backup&&v.reduced_motion&&v.menu_fullscreen&&v.menu_opacity==85,"usability round trip");
 // Own temporary test tree only.
 check(fs::weakly_canonical(data).parent_path()==fs::weakly_canonical(base)&&data.filename().string().starts_with("wonder-profiles-test-"),"cleanup confinement");
 fs::remove_all(data);
 std::puts("profile isolation, UTF-8 validation, restore safety and usability persistence passed");
}
