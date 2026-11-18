#include "launcher/save_backup.h"
#include <cstdlib>
#include <chrono>
#include <fstream>
#include <cstdio>
static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("wonder-backup-test-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "user/save/test");
    fs::create_directories(root / "game");
    { std::ofstream f(root / "user/save/test/progress.dat"); f << "first save"; }
    { std::ofstream f(root / "game/asset.bin"); f << "never included"; }
    std::string error;
    const auto first = nx::launcher::backup_saves(root, error);
    check(!first.empty() && error.empty(), "first backup succeeds");
    { std::ofstream f(root / "user/save/test/progress.dat"); f << "second save"; }
    const auto second = nx::launcher::backup_saves(root, error);
    check(!second.empty() && first != second, "repeated backups use separate directories");
    std::string text;
    { std::ifstream f(first / "user/save/test/progress.dat"); std::getline(f, text); }
    check(text == "first save", "existing backup is unchanged");
    { std::ifstream f(root / "user/save/test/progress.dat"); std::getline(f, text); }
    check(text == "second save", "active save is unchanged");
    check(!fs::exists(second / "game"), "game assets are excluded");
    check(nx::launcher::backup_saves(root / "missing", error).empty() && !error.empty(), "missing saves report failure");
    // This test owns this freshly created temporary directory exclusively.
    check(fs::weakly_canonical(root).parent_path() == fs::weakly_canonical(fs::temp_directory_path()) &&
          root.filename().string().starts_with("wonder-backup-test-"), "cleanup stays inside the test temporary directory");
    fs::remove_all(root);
    std::puts("save backup isolation and preservation tests passed");
}
