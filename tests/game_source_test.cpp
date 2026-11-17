#include "launcher/game_source.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>

void check(bool ok, const char* why) {
    if (!ok) { std::cerr << why << '\n'; std::exit(1); }
}
int main() {
    namespace fs = std::filesystem;
    namespace source = nx::launcher::source;
    const auto root = fs::temp_directory_path() / ("wonder-source-test-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    auto file = [](const fs::path& p) { fs::create_directories(p.parent_path()); std::ofstream(p) << "fixture"; };
    const auto dump = root / fs::path(u8"Wonder ä extraction") / "nested";
    file(dump / "ExeFS/main"); file(dump / "ExeFS/sdk");
    fs::create_directories(dump / "RomFS/Shader");
    check(!source::romfs_ok(dump / "RomFS"), "empty Shader must fail before the build");
    file(dump / "RomFS/Shader/cache.bin");
    check(!source::romfs_ok(dump / "RomFS"), "shader cache is not extracted game content");
    file(dump / "RomFS/Shader/game.bfsha.zs");
    auto found = source::discover(root);
    check(found.exefs.size() == 1 && found.romfs.size() == 1, "find nested Unicode source paths from one parent");
    auto sibling = source::discover(dump / "RomFS");
    check(sibling.exefs == found.exefs && sibling.romfs == found.romfs, "selecting RomFS finds sibling ExeFS");
    sibling = source::discover(dump / "ExeFS");
    check(sibling.exefs == found.exefs && sibling.romfs == found.romfs, "selecting ExeFS finds sibling RomFS");
#if defined(_WIN32)
    sibling = source::discover(dump / "romfs");
    check(sibling.exefs.size() == 1 && sibling.romfs.size() == 1, "case variants are not duplicate Windows candidates");
#endif
    file(root / "second/ExeFS/main"); file(root / "second/ExeFS/sdk");
    file(root / "second/RomFS/Shader/game.zs");
    found = source::discover(root);
    check(found.exefs.size() == 2 && found.romfs.size() == 2, "multiple dumps remain explicit candidates");
    fs::create_directories(root / "invalid/main"); file(root / "invalid/sdk");
    check(!source::exefs_ok(root / "invalid"), "main must be a file, not a folder");
    check(source::discover(root / "missing").romfs.empty(), "missing paths fail without throwing");
    file(root / "combined/main"); file(root / "combined/sdk");
    file(root / "combined/romfs/Shader/game.zs");
    auto combined = source::discover(root / "combined");
    check(combined.exefs.size() == 1 && combined.romfs.size() == 1, "loose main + sdk beside a RomFS subfolder");
    file(root / "kit-layout/game-build/fixture/main"); file(root / "kit-layout/game-build/fixture/sdk");
    check(source::discover(root / "kit-layout").exefs.empty(), "bundled compiler kit is not scanned as game data");
    file(root / "deep/a/b/c/d/e/RomFS/Shader/game.zs");
    auto bounded = source::discover(root / "deep");
    check(bounded.limited && bounded.romfs.empty(), "deep searches stop with an explicit limit result");
    check(root.is_absolute() && root.parent_path() == fs::temp_directory_path()
          && root.filename().string().starts_with("wonder-source-test-"), "cleanup stays inside this test's fixture");
    fs::remove_all(root);
    std::cout << "game source discovery tests passed\n";
}
