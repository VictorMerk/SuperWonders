#include "save_backup.h"
#include "host_path.h"
#include "mod_overlay.h"
#include "launcher/update_crypto.h"
#include <chrono>
#include <array>
#include <fstream>
#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <sstream>

namespace nx::launcher {
namespace fs=std::filesystem;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool linked(const fs::path& p){return nx::mods::linked(fs::directory_entry(host_path(p)));}
struct Entry {
    fs::path relative;uint64_t size=0;fs::file_time_type modified{};bool directory=false;
    bool operator==(const Entry&)const=default;
};
std::vector<Entry> scan(const fs::path& source,const SaveCheckpoint& checkpoint){
    if(checkpoint)checkpoint();
    require(!linked(source)&&fs::is_directory(source),"Save folder must be a real directory.");
    std::vector<Entry> entries;uint64_t bytes=0;size_t visited=0,path_bytes=0;
    for(fs::recursive_directory_iterator it(source),end;it!=end;++it){
        if(checkpoint)checkpoint();
        require(++visited<=16384&&it.depth()<=64,"Save tree is too large or deeply nested.");
        require(!linked(it->path()),"Save folders must not contain links or junctions.");
        const auto relative=it->path().lexically_relative(source);
        path_bytes+=relative.generic_u8string().size();
        require(path_bytes<=(2u<<20),"Save path inventory exceeds backup metadata limit.");
        require(relative.generic_u8string().size()<=4096,"Save filename is too long for backup metadata.");
        if(it->is_directory())entries.push_back({relative,0,{},true});
        else {
            require(it->is_regular_file(),"Unsupported save file type.");
            const auto size=it->file_size();require(size<=(1ull<<30)-bytes,"Save folder exceeds 1 GiB.");bytes+=size;
            entries.push_back({relative,size,it->last_write_time(),false});
        }
    }
    std::sort(entries.begin(),entries.end(),[](const auto&a,const auto&b){return a.relative<b.relative;});return entries;
}
std::string hex(std::string_view text){static const char* digits="0123456789abcdef";std::string out;out.reserve(text.size()*2);for(unsigned char c:text){out+=digits[c>>4];out+=digits[c&15];}return out;}
std::string unhex(std::string_view text){
    require(text.size()%2==0,"Invalid backup integrity metadata.");std::string out;
    const auto nibble=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
    for(size_t i=0;i<text.size();i+=2){const auto a=nibble(text[i]),b=nibble(text[i+1]);require(a>=0&&b>=0,"Invalid backup integrity encoding.");out+=char(a*16+b);}return out;
}
struct Record{fs::path relative;uint64_t size;std::string hash;bool operator==(const Record&)const=default;};
std::string hash_copy(const fs::path& path,const fs::path& target,uint64_t size,const SaveCheckpoint& checkpoint){
    std::ifstream in(path,std::ios::binary);require(bool(in),"Could not read a save file.");
    std::ofstream out;if(!target.empty()){out.open(target,std::ios::binary);require(bool(out),"Could not create a backup file.");}
    UcSha256 sha;uc_sha256_init(&sha);std::array<char,65536> block{};uint64_t read=0;
    while(in){
        if(checkpoint)checkpoint();in.read(block.data(),block.size());const auto count=size_t(in.gcount());
        require(count<=size-read,"A save grew during copying.");read+=count;uc_sha256_update(&sha,block.data(),count);
        if(!target.empty()){out.write(block.data(),count);require(bool(out),"Writing the backup failed.");}
    }
    require(in.eof()&&read==size,"A save changed during copying.");
    if(!target.empty()){out.flush();require(bool(out),"Flushing the backup failed.");}
    std::array<unsigned char,32> digest{};uc_sha256_final(&sha,digest.data());
    return hex({reinterpret_cast<const char*>(digest.data()),digest.size()});
}
std::vector<Record> copy_tree(const fs::path& from,const fs::path& to,const SaveCheckpoint& checkpoint){
    const auto entries=scan(from,checkpoint);if(!to.empty())require(fs::create_directory(to),"Could not create the backup save folder.");
    std::vector<Record> records;
    for(const auto& e:entries){
        if(checkpoint)checkpoint();const auto source=host_path(from/e.relative),target=to.empty()?fs::path{}:host_path(to/e.relative);
        if(e.directory){if(!to.empty())fs::create_directories(target);continue;}
        require(!linked(source)&&fs::file_size(source)==e.size&&fs::last_write_time(source)==e.modified,"A save changed before copying.");
        if(!to.empty())fs::create_directories(target.parent_path());
        records.push_back({e.relative,e.size,hash_copy(source,target,e.size,checkpoint)});
    }
    require(entries==scan(from,checkpoint),"Save inventory changed during copying. Close other game instances and retry.");return records;
}
void write_manifest(const fs::path& backup,const std::vector<Record>& records){
    std::ofstream out(backup/"integrity.txt",std::ios::binary);require(bool(out),"Could not create backup integrity record.");out<<"SWBACKUP1\n";
    for(const auto&r:records){const auto name=r.relative.generic_u8string();out<<r.size<<' '<<r.hash<<' '<<hex({reinterpret_cast<const char*>(name.data()),name.size()})<<'\n';}
    out.flush();require(bool(out),"Could not save backup integrity record.");
}
std::vector<Record> read_manifest(const fs::path& backup){
    const auto path=backup/"integrity.txt";require(!linked(path)&&fs::file_size(path)<=(8u<<20),"Invalid backup integrity record.");
    std::ifstream in(path,std::ios::binary);std::string text((std::istreambuf_iterator<char>(in)),{});require(in.eof()||!in.fail(),"Could not read backup integrity record.");
    std::istringstream stream(text);std::string line;std::getline(stream,line);require(line=="SWBACKUP1","Unknown backup integrity format.");
    std::vector<Record> records;uint64_t total=0;
    while(std::getline(stream,line)){
        std::istringstream fields(line);std::string size,hash,name,extra;require(bool(fields>>size>>hash>>name)&&!(fields>>extra),"Invalid backup integrity entry.");
        uint64_t bytes=0;const auto parsed=std::from_chars(size.data(),size.data()+size.size(),bytes);
        require(parsed.ec==std::errc{}&&parsed.ptr==size.data()+size.size()&&bytes<=(1ull<<30)-total&&records.size()<16384,"Invalid backup size metadata.");
        require(hash.size()==64&&unhex(hash).size()==32&&name.size()<=8192,"Invalid backup checksum metadata.");
        const auto relative=fs::u8path(unhex(name));require(!relative.empty()&&!relative.has_root_path(),"Invalid backup path metadata.");
        for(const auto&part:relative)require(part!=".."&&part!=".","Invalid backup path metadata.");
        total+=bytes;records.push_back({relative,bytes,std::move(hash)});
    }
    std::sort(records.begin(),records.end(),[](const auto&a,const auto&b){return a.relative<b.relative;});
    for(size_t i=1;i<records.size();++i)require(records[i-1].relative!=records[i].relative,"Duplicate backup metadata.");return records;
}
bool remove_partial(const fs::path& path)noexcept{
    if(path.empty())return true;
    try{std::error_code ec;if(fs::exists(path,ec)){if(linked(path))return false;fs::remove_all(path,ec);}return !ec;}
    catch(...){return false;}
}
}
fs::path backup_saves(const fs::path& data_path,std::string& error,const SaveCheckpoint& checkpoint){
    error.clear();fs::path temporary;
    try{
        const auto data=host_path(data_path),source=host_path(data/"user"),backups=host_path(data/"backups");
        require(fs::is_directory(source),"No save folder exists yet.");require(!linked(source),"Save folder must not be linked.");
        fs::create_directories(backups);require(!linked(backups),"Backups folder must not be linked.");
        const auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        fs::path destination;
        for(unsigned attempt=0;attempt<100;++attempt){const auto name="saves-"+std::to_string(stamp)+"-"+std::to_string(attempt);destination=backups/name;
            if(fs::exists(destination))continue;const auto candidate=backups/(name+".partial");if(fs::create_directory(candidate)){temporary=candidate;break;}}
        require(!temporary.empty(),"Could not allocate a new backup folder.");
        write_manifest(temporary,copy_tree(source,temporary/"user",checkpoint));if(checkpoint)checkpoint();fs::rename(temporary,destination);return data_path/"backups"/destination.filename();
    }catch(const std::exception&e){const bool cleaned=remove_partial(temporary);error=std::string("Backup incomplete; active saves unchanged. ")+e.what();if(!cleaned)error+=" A partial copy remains in backups.";return {};}
}
std::vector<fs::path> list_save_backups(const fs::path& data_path){
    const auto base=host_path(data_path/"backups");std::vector<fs::path> result;std::error_code ec;
    if(!fs::is_directory(base,ec)||linked(base))return result;
    for(fs::directory_iterator it(base,ec),end;!ec&&it!=end;it.increment(ec)){
        if(result.size()>=4096)break;if(it->path().extension()==".partial"||linked(it->path()))continue;
        if(fs::is_directory(host_path(it->path()/"user/save"),ec))result.push_back(data_path/"backups"/it->path().filename());
    }
    std::sort(result.rbegin(),result.rend());return result;
}
BackupSummary backup_summary(const fs::path& backup_path,std::string& error){
    error.clear();BackupSummary summary;
    try{const auto backup=host_path(backup_path);require(!linked(backup),"Backup folder must not be linked.");
        summary.has_integrity=fs::exists(backup/"integrity.txt");
        if(summary.has_integrity){for(const auto&r:read_manifest(backup)){++summary.files;summary.bytes+=r.size;}}
        else{for(const auto&e:scan(backup/"user",{}))if(!e.directory){++summary.files;summary.bytes+=e.size;}}
    }catch(const std::exception&e){error=e.what();}return summary;
}
bool verify_save_backup(const fs::path& backup_path,std::string& error,const SaveCheckpoint& checkpoint){
    error.clear();try{const auto backup=host_path(backup_path);require(!linked(backup),"Backup folder must not be linked.");
        require(fs::exists(backup/"integrity.txt"),"Legacy backup has no integrity record; it cannot be checksum verified.");
        require(read_manifest(backup)==copy_tree(backup/"user",{},checkpoint),"Backup checksum or file inventory does not match.");return true;
    }catch(const std::exception&e){error=e.what();return false;}
}
bool restore_saves(const fs::path& data_path,const fs::path& backup_path,std::string& error,const SaveCheckpoint& checkpoint){
    error.clear();fs::path stage,safety;bool moved=false,stage_owned=false;const auto data=host_path(data_path),backup=host_path(backup_path);
    try{
        const auto backups=host_path(data/"backups");require(!linked(backups),"Backups folder must not be linked.");
        require(backup.parent_path().lexically_normal()==backups.lexically_normal()&&backup.extension()!=".partial"&&!linked(backup)&&fs::is_directory(host_path(backup/"user/save")),"Choose a completed backup from this profile.");
        const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();stage=backups/("restore-"+std::to_string(stamp)+".partial");require(fs::create_directory(stage),"Could not prepare restore folder.");stage_owned=true;
        const auto records=copy_tree(backup/"user",stage/"user",checkpoint);
        if(fs::exists(backup/"integrity.txt"))require(records==read_manifest(backup),"Backup checksum or file inventory does not match. Active saves unchanged.");
        if(checkpoint)checkpoint();
        const auto user=host_path(data/"user");const bool have_user=fs::exists(user);
        if(have_user){safety=backup_saves(data,error,checkpoint);if(safety.empty())throw std::runtime_error(error.empty()?"Could not preserve current progress before restore.":error);if(checkpoint)checkpoint();fs::rename(user,host_path(safety/"original-user"));moved=true;}
        try{fs::rename(stage/"user",user);}catch(...){if(moved){std::error_code rollback;fs::rename(host_path(safety/"original-user"),user,rollback);if(rollback){error="Restore failed. Previous progress remains in the safety backup; reopen the launcher before playing.";remove_partial(stage);return false;}}throw;}
        remove_partial(stage);return true;
    }catch(const std::exception&e){const bool cleaned=!stage_owned||remove_partial(stage);error=std::string("Restore failed; backup preserved. ")+e.what();if(!cleaned)error+=" A partial restore remains in backups.";return false;}
}
}
