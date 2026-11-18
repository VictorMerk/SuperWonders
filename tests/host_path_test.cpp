#include "host_path.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
int main() {
    namespace fs = std::filesystem;
    const auto temporary = fs::temp_directory_path();
    const auto root = temporary / ("wonder-host-path-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto nested = root;
    while (nested.native().size() < 320) nested /= "resource-directory-with-a-long-name";
    const auto file = nested / fs::path(u8"resource-\u00e4.bgyml");
    fs::create_directories(nx::host_path(nested));
    { std::ofstream out(nx::host_path(file), std::ios::binary); out << "event-resource"; check(bool(out), "create long Unicode resource"); }
    check(fs::is_regular_file(nx::host_path(file)), "metadata finds long resource");
    check(fs::file_size(nx::host_path(file)) == 14, "long resource metadata size");
#if defined(_WIN32)
    FILE* stream = _wfopen(nx::host_path(file).c_str(), L"rb");
#else
    FILE* stream = std::fopen(nx::host_path(file).c_str(), "rb");
#endif
    check(stream != nullptr, "CRT opens the same long resource as filesystem metadata");
    char contents[15]{};
    check(std::fread(contents, 1, 14, stream) == 14 && std::string(contents) == "event-resource", "long resource content preserved");
    std::fclose(stream);
    check(nx::host_path(nx::host_path(file)) == nx::host_path(file), "extended path is idempotent");
#if defined(_WIN32)
    check(nx::host_path(L"\\\\server\\share\\resource") == fs::path(L"\\\\?\\UNC\\server\\share\\resource"), "UNC path preserves share");
    check(nx::host_path(L"\\\\.\\pipe\\test") == fs::path(L"\\\\.\\pipe\\test"), "device path preserved");
#endif
    check(root.parent_path() == temporary && root.filename().string().starts_with("wonder-host-path-test-"), "cleanup confined to test directory");
    fs::remove_all(nx::host_path(root));
    std::puts("long Unicode resource metadata and CRT reads passed");
}
