#include "shader_bundle.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>

static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr,"%s\n",message); std::exit(1); }
}
static void write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path,std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
}
static std::vector<uint8_t> read(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(in),{}};
}
int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--pack") {
        std::string error;
        const auto started = std::chrono::steady_clock::now();
        const auto data = std::filesystem::path(reinterpret_cast<const char8_t*>(argv[2]));
        const bool ok = nx::nvn::build_shader_bundle(data,error);
        std::printf("Shader bundle %s in %.3f s%s%s\n",ok ? "prepared" : "failed",
            std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count(),error.empty() ? "" : ": ",error.c_str());
        return ok ? 0 : 1;
    }
    namespace fs = std::filesystem;
    using nx::nvn::ShaderBundle;
    const auto root = fs::temp_directory_path()/("wonder-shader-bundle-test-"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root/"spirv");
    std::vector<uint8_t> a(24,0), b(24,0);
    a[0]=b[0]=3; a[1]=b[1]=2; a[2]=b[2]=35; a[3]=b[3]=7; a[20]=1; b[20]=2;
    const std::string one = "0000000000000001", two = "0000000000000002";
    write(root/"spirv"/(one+".spv"),a); write(root/"spirv"/(two+".spv"),b);
    std::string error; uint64_t done = 0;
    check(nx::nvn::build_shader_bundle(root,error,[&](uint64_t n,uint64_t) { done=n; }),"build archive");
    check(done == 2 && error.empty(),"progress reaches the shader count");
    check(ShaderBundle(root,false).memory_bytes() == 0,"cache can be disabled without loading its bytes");
    ShaderBundle cache(root);
    check(cache.size() == 2 && cache.find(one).size() == a.size(),"load indexed shaders");
    check(std::equal(a.begin(),a.end(),cache.find(one).begin()),"shader bytes are preserved");
    check(cache.find("../not-a-hash").empty(),"names cannot escape the shader directory");
    auto corrupted = read(root/"spirv.pack"); corrupted.back() ^= 1; write(root/"spirv.pack",corrupted);
    ShaderBundle damaged(root);
    check(damaged.find(two).empty() && !damaged.find(one).empty(),"damaged shader falls back without losing other entries");
    check(nx::nvn::build_shader_bundle(root,error),"rebuild replaces an existing archive");
    auto updated = a; updated[20]=3; write(root/"spirv"/(one+".spv"),updated);
    fs::last_write_time(root/"spirv"/(one+".spv"),fs::last_write_time(root/"spirv"/(one+".spv"))+std::chrono::seconds(2));
    check(cache.find(one).empty(),"edited source never uses stale cached code");
    fs::remove(root/"spirv"/(two+".spv"));
    check(cache.find(two).empty(),"removed source never uses cached code");
    const auto before = read(root/"spirv.pack");
    updated[0]=0; write(root/"spirv"/(one+".spv"),updated);
    check(!nx::nvn::build_shader_bundle(root,error) && !error.empty(),"invalid source rejects rebuilding");
    check(read(root/"spirv.pack") == before,"failed rebuilding preserves the existing archive");
    corrupted=before; for (size_t i=24;i<32;++i) corrupted[i]=255; write(root/"spirv.pack",corrupted);
    check(ShaderBundle(root).size() == 0,"out-of-bounds index is rejected");
    const auto incremental = root/"incremental";
    fs::create_directories(incremental/"spirv");
    write(incremental/"spirv"/(one+".spv"),a);
    {
        ShaderBundle first(incremental);
        check(first.load(one) == a,"first launch reads original shaders");
        first.flush();
        const auto once = fs::file_size(incremental/"spirv.journal");
        check(first.load(one) == a,"repeated reads preserve code");
        first.flush();
        check(fs::file_size(incremental/"spirv.journal") == once,"repeated reads do not duplicate journal entries");
        ShaderBundle second(incremental);
        check(second.find(one).size() == a.size(),"next launch reads incremental cache");
        write(incremental/"spirv"/(two+".spv"),b);
        check(second.load(two) == b,"another instance falls back while the writer is locked");
        second.flush();
        check(fs::file_size(incremental/"spirv.journal") == once,"another instance cannot change the locked journal");
    }
    auto journal = read(incremental/"spirv.journal");
    journal.back() ^= 1; journal.insert(journal.end(),{1,2,3,4,5});
    write(incremental/"spirv.journal",journal);
    {
        ShaderBundle damaged_journal(incremental);
        check(damaged_journal.find(one).empty(),"journal payload corruption misses safely");
        check(damaged_journal.load(one) == a && damaged_journal.load(two) == b,"damaged journal falls back to original shaders");
        damaged_journal.flush();
    }
    {
        ShaderBundle repaired(incremental);
        check(repaired.find(one).size() == a.size() && repaired.find(two).size() == b.size(),"incomplete tail repaired and later records remain readable");
        auto changed = b; changed[20] = 9;
        write(incremental/"spirv"/(two+".spv"),changed);
        fs::last_write_time(incremental/"spirv"/(two+".spv"),fs::last_write_time(incremental/"spirv"/(two+".spv"))+std::chrono::seconds(2));
        check(repaired.find(two).empty() && repaired.load(two) == changed,"incremental cache invalidates edited sources");
    }
    {
        ShaderBundle updated_journal(incremental);
        check(updated_journal.find(two)[20] == 9,"latest valid incremental record wins");
        const auto before_disabled = read(incremental/"spirv.journal");
        ShaderBundle disabled(incremental,false);
        check(disabled.find(one).empty() && disabled.load(one) == a,"disabled cache loads original files");
        disabled.flush();
        check(read(incremental/"spirv.journal") == before_disabled,"disabled cache writes nothing");
    }
    check(fs::weakly_canonical(root).parent_path() == fs::weakly_canonical(fs::temp_directory_path()) &&
          root.filename().string().starts_with("wonder-shader-bundle-test-"),"cleanup stays inside test temporary directory");
    fs::remove_all(root);
    std::puts("shader bundle: roundtrip, updates, corruption, bounds and atomic replacement passed");
}
