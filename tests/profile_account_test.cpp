#include "ipc.h"
#include "profiles.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <array>
#include <algorithm>
namespace {
std::string root;
void check(bool ok,const char* text){if(!ok){std::fprintf(stderr,"%s\n",text);std::exit(1);}}
}
namespace nx {
ServiceFactory& account_factory(){static ServiceFactory f;return f;}
void register_service(const std::string& name,ServiceFactory f){if(name=="acc:u0")account_factory()=std::move(f);}
uint32_t Service::handle(IpcCtx& c){return cmds.at(c.cmd)(c);}
namespace plat {std::string data_root(){return root;}}
}
int main(){
 namespace fs=std::filesystem;
 const auto temporary=fs::temp_directory_path();
 const auto data=temporary/("wonder-account-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 fs::create_directories(data);const auto text=data.u8string();root.assign(reinterpret_cast<const char*>(text.data()),text.size());
 std::string error;check(nx::profiles::rename(data,"Victor",error),"rename profile");
 auto service=nx::account_factory()();nx::IpcCtx user;user.cmd=4;
 check(service->handle(user)==0&&user.out.size()==16,"account UID available");
 uint64_t low=0,high=0;std::memcpy(&low,user.out.data(),8);std::memcpy(&high,user.out.data()+8,8);
 check(low==1&&high==0x4e58485553455231ull,"legacy account UID preserved");
 std::array<uint8_t,144> context;context.fill(0xa5);
 nx::IpcCtx stored;stored.cmd=131;stored.c.push_back({reinterpret_cast<uint64_t>(context.data()+8),128});
 check(service->handle(stored)==0,"cold retained-user query succeeds");
 check(std::all_of(context.begin()+8,context.end()-8,[](uint8_t b){return b==0;}),"cold launch clears every retained UID, including poisoned stack contents");
 check(std::all_of(context.begin(),context.begin()+8,[](uint8_t b){return b==0xa5;})&&std::all_of(context.end()-8,context.end(),[](uint8_t b){return b==0xa5;}),"retained-user output respects buffer bounds");
 nx::IpcCtx absent;absent.cmd=130;
 check(service->handle(absent)!=0&&absent.out_objects.empty(),"absent retained context cannot return empty success");
 auto base=[&](){nx::IpcCtx p;p.cmd=5;check(service->handle(p)==0&&p.out_objects.size()==1,"profile service available");nx::IpcCtx c;c.cmd=1;check(p.out_objects[0]->handle(c)==0&&c.out.size()==56,"profile base size");return c.out;};
 auto first=base();check(std::memcmp(first.data(),user.out.data(),16)==0,"profile UID matches selected account");
 check(std::string(reinterpret_cast<const char*>(first.data()+24))=="Victor","guest receives configured nickname");
 check(nx::profiles::create(data,"Brother",error),"create second profile");
 auto second=base();check(std::string(reinterpret_cast<const char*>(second.data()+24))=="Brother","guest receives selected profile nickname");
 check(std::memcmp(first.data(),second.data(),16)==0,"separate storage does not change guest UID layout");
 check(fs::weakly_canonical(data).parent_path()==fs::weakly_canonical(temporary)&&data.filename().string().starts_with("wonder-account-test-"),"cleanup confined");fs::remove_all(data);
 std::puts("guest profile nickname and legacy UID compatibility passed");
}
