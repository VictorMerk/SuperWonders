#include "launcher/mod_catalog.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>

void check(bool pass,const char* message) {
    if (!pass) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path()/("wonder-mod-test-"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    auto file = [&](const fs::path& p) { fs::create_directories(p.parent_path()); std::ofstream(p) << "test"; };
    file(root/"01-base/romfs/Model/Test.bin");
    file(root/"02-overlay/contents/010015100B514000/romfs/model/test.bin");
    file(root/"02-overlay/contents/010015100B514000/romfs/extra.bin");
    file(root/"-00-disabled/romfs/Model/Test.bin");
    file(root/"03-invalid/readme.txt");
    auto mods = nx::launcher::inspect_mods(root);
    check(mods.size()==4,"all mod folders listed");
    check(!mods[0].on && mods[0].files==1 && mods[0].shadowed==0,"disabled mod counted but never owns a file");
    check(mods[1].name=="01-base" && mods[1].files==1 && mods[1].bytes==4,"simple romfs layout and file sizes");
    check(mods[2].files==2 && mods[2].shadowed==1 && mods[2].conflicts[0].find("01-base")!=std::string::npos,
          "nested layout and case-insensitive first-mod-wins conflict");
    check(!mods[3].issue.empty() && mods[3].romfs.empty(),"malformed layout explained");
    fs::rename(root/"01-base",root/"-01-base");
    mods = nx::launcher::inspect_mods(root);
    check(mods[2].shadowed==0,"disabling winning mod resolves conflict on rescan");
    // Cleanup is restricted to the unique directory created by this test.
    check(root.is_absolute() && root.parent_path()==fs::temp_directory_path() &&
          root.filename().string().starts_with("wonder-mod-test-"),"cleanup target is the test's own temp directory");
    fs::remove_all(root);
    std::cout << "mod catalogue tests passed\n";
}
