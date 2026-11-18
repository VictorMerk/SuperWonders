#include "cache_file.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>

static void check(bool value,const char* message) { if(!value) { std::fprintf(stderr,"%s\n",message); std::exit(1); } }
static std::filesystem::path path(const std::string& value) { return std::filesystem::path(reinterpret_cast<const char8_t*>(value.c_str())); }
static void write(const std::filesystem::path& destination,const char* text) { std::ofstream out(destination,std::ios::binary); out<<text; }
static std::string read(const std::filesystem::path& source) { std::ifstream in(source,std::ios::binary); return {std::istreambuf_iterator<char>(in),{}}; }
int main() {
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("wonder-cache-file-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    const auto target=root/fs::path(u8"warm-cache-\u00e4.bin");
    const auto encoded=target.u8string();
    const std::string name(reinterpret_cast<const char*>(encoded.data()),encoded.size());
    write(target,"previous warm cache");
    {
        nx::nvn::CacheFile interrupted(name);
        check(!interrupted.commit(),"a missing pending file cannot replace a working cache");
        check(read(target)=="previous warm cache","failed writes preserve the complete previous cache");
    }
    {
        nx::nvn::CacheFile first(name),second(name);
        check(first.path()!=second.path(),"concurrent writers own different temporary files");
        write(path(first.path()),"first complete cache"); write(path(second.path()),"second complete cache");
        check(read(target)=="previous warm cache","pending writes do not expose partial data");
        check(first.commit() && read(target)=="first complete cache","atomic replacement supports existing destinations");
        check(second.commit() && read(target)=="second complete cache","another writer publishes only its own complete file");
    }
    size_t files=0; for(const auto& entry:fs::directory_iterator(root)) { ++files; check(entry.path()==target,"owned temporary paths are cleaned up"); }
    check(files==1,"only the published cache remains");
    check(fs::weakly_canonical(root).parent_path()==fs::weakly_canonical(fs::temp_directory_path()) && root.filename().string().starts_with("wonder-cache-file-test-"),"cleanup remains in this test's temporary folder");
    fs::remove_all(root); std::puts("cache writes: interrupted writes, concurrent staging, Unicode paths and atomic replacement passed");
}
