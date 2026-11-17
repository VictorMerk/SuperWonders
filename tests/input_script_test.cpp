#include "platform.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>

static uint64_t now = 0;
namespace nx { void log(const char*, ...) {} }
namespace nx::plat {
uint64_t monotonic_ns() { return now; }
FILE* fopen_utf8(const std::string& path, const char* mode) { return std::fopen(path.c_str(), mode); }
std::string data_root() { return "."; }
}
static void check(bool value) { if (!value) std::abort(); }
int main() {
    using namespace nx::plat;
    const std::string path = (std::filesystem::temp_directory_path() / "wonder-live-input-test.txt").string();
    const std::string source = "@" + path;
#ifdef _WIN32
    _putenv_s("NX_INPUT_SCRIPT", source.c_str());
#else
    setenv("NX_INPUT_SCRIPT", source.c_str(), 1);
#endif
#ifdef _WIN32
    _putenv_s("NX_INPUT_SCRIPT_P2", "0:X:1");
#else
    setenv("NX_INPUT_SCRIPT_P2", "0:X:1", 1);
#endif
    PadState second{}; input_script(second, 1); check(second.buttons == kX);
    auto write = [&](const char* text) { std::ofstream f(path); f << text; };
    auto read = [&] { PadState p{}; input_script(p); return p; };
    write("0:A:0.5;0.25:RIGHT:1");
    check(read().buttons == kA);
    now = 250'000'000;
    auto p = read(); check(p.buttons == (kA | kRight) && p.lx == kStickMax);
    now = 750'000'000;
    check(read().buttons == kRight);
    write("0:B:0.25");
    now = 1'000'000'000;
    check(read().buttons == kB);  // replacement resets script time and releases the old direction
    now = 1'300'000'000;
    check(read().buttons == 0);
    write("0:Y:10");
    now = 1'600'000'000;
    check(read().buttons == kY);
    write("");
    now = 1'900'000'000;
    check(read().buttons == 0);  // empty control file releases held inputs
    second = {}; input_script(second, 1); check(second.buttons == 0);
    write("0:PLUS+MINUS:1;0:RIGHT+B:1");
    now = 2'200'000'000;
    p = read(); check(p.buttons == (kPlus|kMinus|kRight|kB) && p.lx == kStickMax);
    std::remove(path.c_str());
    std::puts("live input replacement/expiry/release tests passed");
}
