#pragma once
#include <filesystem>
namespace nx::launcher {
void open_profiles();
void start_profile_backup();
void start_profile_restore(std::filesystem::path backup);
void draw_profiles(float width,float height);
bool profiles_open();
bool profile_save_busy();
bool backup_before_play();
}
