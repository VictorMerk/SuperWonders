#include "launcher/profile_ui.h"
#include "launcher/save_backup.h"
#include "profiles.h"
#include "platform.h"
#include "netplay_game.h"
#include "overlay/overlay.h"
#include "overlay/overlay_ui.h"
#include "overlay/usability.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cstdio>
#include <chrono>
#include <ctime>
namespace nx::launcher {
namespace {
namespace fs=std::filesystem;
bool requested=false,opened=false,confirm=false;
char name[128]{},new_name[128]{};
std::string message;
std::vector<fs::path> backups;
int selected=-1;
fs::path data() {return fs::u8path(plat::data_root());}
std::string utf8(const fs::path& p) {const auto s=p.u8string();return std::string(reinterpret_cast<const char*>(s.data()),s.size());}
void refresh() {
    const auto p=profiles::active(data()); std::snprintf(name,sizeof name,"%s",p.name.c_str());
    backups=list_save_backups(profiles::storage_root(data())); selected=backups.empty()?-1:0;confirm=false;
}
std::string backup_label(const fs::path& path) {
    std::error_code ec;const auto written=fs::last_write_time(path,ec);
    if(ec)return path.filename().string();
    const auto system=std::chrono::file_clock::to_sys(written);
    const auto stamp=std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(system));
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local,&stamp);
#else
    localtime_r(&stamp,&local);
#endif
    char label[64]{};std::strftime(label,sizeof label,"%Y-%m-%d  %H:%M:%S",&local);
    return label;
}
bool paired() {const auto p=lan::lobby_status().phase;return lan::active() || p==lan::LobbyPhase::Preparing || p==lan::LobbyPhase::Waiting || p==lan::LobbyPhase::Ready;}
}
void open_profiles() {requested=true;opened=true;message.clear();refresh();}
bool profiles_open() {return opened;}
bool backup_before_play() {
    const auto root=profiles::storage_root(data());
    if(!overlay::load_usability(data()).auto_backup || !fs::exists(root/"user/save")) return true;
    std::string error; const auto backup=backup_saves(root,error);
    if(backup.empty()) {overlay::notify(error.c_str());return false;} return true;
}
void draw_profiles(float width,float height) {
    if(requested) {ImGui::OpenPopup("Profiles & saves");requested=false;}
    const float k=std::clamp(height/1080.f,.5f,3.f);
    ImGui::SetNextWindowSize({std::min(width-24*k,1000*k),std::min(height-24*k,900*k)},ImGuiCond_Appearing);
    ImGui::SetNextWindowPos({width*.5f,height*.5f},ImGuiCond_Appearing,{.5f,.5f});
    ImGui::SetNextWindowSizeConstraints({std::min(width-24*k,640*k),std::min(height-24*k,600*k)},{width-24*k,height-24*k});
    if(!ImGui::BeginPopupModal("Profiles & saves",nullptr,ImGuiWindowFlags_NoSavedSettings)) {opened=false;return;}
    opened=true;
    const bool appearing=ImGui::IsWindowAppearing();
    const bool was_editing=ImGui::IsAnyItemActive();
    const bool nested_popup=ImGui::GetCurrentContext()->OpenPopupStack.Size>ImGui::GetCurrentContext()->BeginPopupStack.Size;
    const auto all=profiles::list(data()); const auto current=profiles::active(data());
    const auto storage=profiles::storage_root(data());
    const bool locked=paired();
    ImGui::TextWrapped("Each profile has its own progress. The original profile keeps your existing saves. Switch before starting a game or LAN room.");
    if(locked) ImGui::TextWrapped("Close the LAN room before changing profiles or restoring saves.");
    ImGui::BeginChild("profile-content",{0,-58*k},ImGuiChildFlags_NavFlattened);
    ImGui::BeginDisabled(locked);
    ImGui::SetNextItemWidth(-1);
    if(ImGui::BeginCombo("##active-profile",current.name.c_str())) {
        for(const auto& p:all) {
            if(ImGui::Selectable((p.name+"##"+p.id).c_str(),p.id==current.id)) {
                if(profiles::select(data(),p.id,message)) {refresh();message="Profile selected.";}
            }
            if(p.id==current.id)ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if(appearing) {
        ImGui::SetFocusID(ImGui::GetItemID(),ImGui::GetCurrentWindow());
        ImGui::SetNavCursorVisible(true);
    }
    ImGui::SeparatorText("Profile name");
    ImGui::SetNextItemWidth(-1); ImGui::InputText("##nickname",name,sizeof name);
    if(ImGui::Button("Save name")) {if(profiles::rename(data(),name,message))message="Name saved. It appears in-game on your next launch.";}
    ImGui::SeparatorText("New adventure");
    ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##new-profile","New profile name",new_name,sizeof new_name);
    if(ImGui::Button("Create profile with fresh saves")) {
        if(profiles::create(data(),new_name,message)) {refresh();new_name[0]=0;message="New profile selected. Your other saves are preserved.";}
    }
    ImGui::EndDisabled();
    ImGui::SeparatorText("Save backups");
    if(ImGui::Button("Back up now")) {
        const auto b=backup_saves(storage,message);
        if(!b.empty()) {refresh();message="Backup created.";}
    }
    ImGui::SameLine();if(ImGui::Button("Refresh"))refresh();
    if(ImGui::Button("Open this profile's save folder")) {std::error_code ec;fs::create_directories(storage/"user/save",ec);plat::open_folder(utf8(storage/"user/save"));}
    if(ImGui::Button("Open backups folder")) {std::error_code ec;fs::create_directories(storage/"backups",ec);plat::open_folder(utf8(storage/"backups"));}
    ImGui::SetNextItemWidth(-1);
    const auto preview=selected>=0 && selected<int(backups.size())?backup_label(backups[selected]):"No backups yet";
    if(ImGui::BeginCombo("##backup-list",preview.c_str())) {
        for(int i=0;i<int(backups.size());++i) if(ImGui::Selectable((backup_label(backups[i])+"##"+backups[i].filename().string()).c_str(),selected==i)) {selected=i;confirm=false;}
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(locked || selected<0);
    if(ImGui::Button("Restore selected backup..."))confirm=true;
    if(confirm) {
        ImGui::TextWrapped("Replace progress for %s? Current progress is backed up first. This does not change any other profile.",current.name.c_str());
        if(ImGui::Button("Confirm restore")) {
            if(restore_saves(storage,backups.at(selected),message)) {refresh();message="Backup restored. Previous progress is preserved in a safety backup.";}
            confirm=false;
        }
        ImGui::SameLine();if(ImGui::Button("Keep current progress"))confirm=false;
    }
    ImGui::EndDisabled();
    auto& preferences=overlay::usability_preferences();
    if(ImGui::Checkbox("Back up saves before Play",&preferences.auto_backup)) {
        if(!overlay::save_usability(data(),preferences))message="Could not save backup preference.";
    }
    ImGui::TextWrapped("Automatic backups stay on disk until you remove them. Backups contain progress, not game files. A LAN guest uses a separate session copy.");
    if(!message.empty()) ImGui::TextWrapped("%s",message.c_str());
    ImGui::EndChild();
    if(ImGui::Button("Done",{140*k,0}) || ((ImGui::IsKeyPressed(ImGuiKey_Escape,false)||ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight,false))&&!was_editing&&!nested_popup)) {
        opened=false;confirm=false;ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
}
