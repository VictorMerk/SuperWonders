#include "launcher/save_backup.h"
#include "host_path.h"
#include <stdexcept>
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
    check(nx::launcher::verify_save_backup(first,error),"completed backups have valid content checksums");
    const auto summary=nx::launcher::backup_summary(first,error);
    check(summary.has_integrity&&summary.files==1&&summary.bytes==10,"backup preview uses bounded integrity metadata");
    {std::ofstream f(first/"user/save/test/progress.dat");f<<"bad!! save";}
    check(!nx::launcher::verify_save_backup(first,error),"same-size corruption invalidates backup integrity");
    check(!nx::launcher::restore_saves(root,first,error),"corrupted backup is rejected before replacing active progress");
    {std::ifstream f(root/"user/save/test/progress.dat");std::getline(f,text);}
    check(text=="second save","failed integrity check preserves active progress");
    {std::ofstream f(first/"user/save/test/progress.dat");f<<"first save";}
    check(nx::launcher::restore_saves(root,first,error),"verified backup restores with a safety copy");
    const auto no_partial=[&]{for(const auto&e:fs::directory_iterator(root/"backups"))if(e.path().extension()==".partial")return false;return true;};
    bool interrupted=false;
    const auto interrupt_after_write=[&]{for(const auto&e:fs::directory_iterator(root/"backups"))if(e.path().extension()==".partial"){
        const auto file=e.path()/"user/save/test/progress.dat";
        if(fs::is_regular_file(file)&&fs::file_size(file)>0){interrupted=true;throw std::runtime_error("cancel test");}
    }};
    check(nx::launcher::backup_saves(root,error,interrupt_after_write).empty()&&interrupted&&no_partial(),"cancelling a written backup cleans its owned partial folder");
    interrupted=false;
    check(!nx::launcher::restore_saves(root,first,error,interrupt_after_write)&&interrupted&&no_partial(),"cancelling staged restore removes only its own stage");
    bool added=false;
    check(nx::launcher::backup_saves(root,error,[&]{if(!added){for(const auto&e:fs::directory_iterator(root/"backups"))if(e.path().extension()==".partial"&&fs::exists(e.path()/"user/save/test/progress.dat")){
        std::ofstream(root/"user/save/added.dat")<<"new";added=true;break;
    }}}).empty()&&added&&no_partial(),"a changing source inventory cannot become a completed backup");
    fs::remove(root/"user/save/added.dat");
    const auto legacy=root/"backups/legacy";fs::create_directories(legacy/"user/save");std::ofstream(legacy/"user/save/legacy.dat")<<"legacy";
    check(!nx::launcher::verify_save_backup(legacy,error),"legacy backups are not mislabeled checksum verified");
    check(nx::launcher::restore_saves(root,legacy,error),"legacy backups remain restorable through a checked staged copy");
    const auto unrelated=root/"backups/unrelated.partial";fs::create_directories(unrelated);std::ofstream(unrelated/"keep.dat")<<"another operation";
    check(!nx::launcher::restore_saves(root,first,error,[]{throw std::runtime_error("cancel isolated restore");})&&fs::exists(unrelated/"keep.dat"),"restore cleanup leaves other operations' partial directories untouched");
    fs::remove_all(unrelated);
    auto long_root=root/fs::path(u"long-\u65e5\u672c");for(int i=0;i<5;++i)long_root/=std::string(55,'a'+i);
    fs::create_directories(nx::host_path(long_root/"user/save/nested"));std::ofstream(nx::host_path(long_root/"user/save/nested/save.dat"))<<"long path";
    const auto long_backup=nx::launcher::backup_saves(long_root,error);
    check(!long_backup.empty()&&nx::launcher::verify_save_backup(long_backup,error)&&nx::launcher::restore_saves(long_root,long_backup,error),"backup, integrity verification and restore support long Unicode roots");
    // This test owns this freshly created temporary directory exclusively.
    check(fs::weakly_canonical(root).parent_path() == fs::weakly_canonical(fs::temp_directory_path()) &&
          root.filename().string().starts_with("wonder-backup-test-"), "cleanup stays inside the test temporary directory");
    fs::remove_all(nx::host_path(root));
    std::puts("save backup isolation and preservation tests passed");
}
