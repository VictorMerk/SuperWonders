#include "nvn/pipeline_miss_trace.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
using namespace nx::nvn::pipeline_trace;
void check(bool ok,const char* why) {if(!ok){std::fprintf(stderr,"pipeline trace: %s\n",why);std::exit(1);}}
int main(int argc,char** argv) {
    if(argc==2 && std::string_view(argv[1])=="--disabled") {
#ifdef _WIN32
        _putenv_s("NX_PIPELINE_MISS_TRACE","");
#else
        unsetenv("NX_PIPELINE_MISS_TRACE");
#endif
        check(!enabled(),"absent opt-in remains disabled");
        capture({},Operation::Miss);shutdown();shutdown();
        std::puts("Pipeline miss trace: absent opt-in capture/shutdown remain disabled");
        return 0;
    }
    const auto id=identity(42,71,"0123456789abcdef","ABCDEF0123456789",true,false,true);
    check(std::string(id.vs)=="0123456789abcdef" && std::string(id.fs)=="ABCDEF0123456789","shader hashes preserved");
    const auto bad=identity(1,2,"malicious,\nlabel!","0123456789abcdeg",false,false,false);
    check(!bad.vs[0] && !bad.fs[0],"nonhash strings cannot inject CSV fields");
    {
        Recorder r(3);
        for(unsigned i=0;i<7;++i)r.capture({id,Operation::SyncCompile,0,i,100+i,-1,true});
        std::ostringstream out;r.freeze_and_write(out);const auto text=out.str();
        check(text.find("total_events=7,retained_events=3,evicted_events=4,capacity=3")!=std::string::npos,"bounded ring reports exact eviction");
        check(text.find("5,4,71,42,0123456789abcdef,ABCDEF0123456789,1,0,1,sync_compile,104,1,-1")!=std::string::npos,"chronological retained events contain identity/policy/result");
        check(text.find("4,3,71,42,")==std::string::npos,"evicted events are absent");
        r.capture({id,Operation::Miss});std::ostringstream again;r.freeze_and_write(again);
        check(again.str()==text,"late producers cannot modify frozen report");
        check(text.find("durations_overlap")!=std::string::npos && text.find("noncryptographic")!=std::string::npos &&
              text.find("process_local_type_ids_do_not_compare_alone_across_runs")!=std::string::npos,"timing and fingerprint limits explicit");
    }
    {
        Recorder r(2);r.capture({id,Operation::DrawSkipped});std::ostringstream out;r.freeze_and_write(out);
        check(out.str().find("draw_skipped_pipeline_pending,0,0,\n")!=std::string::npos,
              "actual omitted draw has distinct operation and no invented timing or result");
    }
    {
        Recorder r(0);r.capture({});std::ostringstream out;r.freeze_and_write(out);
        check(out.str().find("total_events=1,retained_events=0,evicted_events=1,capacity=0")!=std::string::npos,"zero-capacity ring safely counts loss");
    }
    {
        Recorder r(257);std::vector<std::thread> workers;
        for(unsigned worker=0;worker<6;++worker)workers.emplace_back([&,worker]{
            for(unsigned i=0;i<2000;++i)r.capture({identity(worker,i,"0123456789abcdef","",false,false,false),Operation::AsyncObserved,0,0,1234,0,true});
        });
        for(auto& worker:workers)worker.join();
        std::ostringstream out;r.freeze_and_write(out);std::istringstream lines(out.str());std::string line;unsigned rows=0;uint64_t previous=0;
        check(out.str().find("total_events=12000,retained_events=257,evicted_events=11743")!=std::string::npos,"concurrent capture loses only declared evictions");
        while(std::getline(lines,line)) {
            if(line.empty() || line[0]=='#' || line.starts_with("sequence,"))continue;
            const auto sequence=std::stoull(line);check(previous==0 || sequence==previous+1,"concurrent retained sequence contiguous");previous=sequence;++rows;
            check(line.find("async_completion_observed,1234,1,0")!=std::string::npos,"worker duration/result retain distinct operation");
        }
        check(rows==257 && previous==12000,"bounded chronological tail complete");
    }
    {
        Recorder r(32);std::atomic<bool> start=false;std::thread producer([&]{while(!start.load()){}for(unsigned i=0;i<10000;++i)r.capture({id,Operation::Miss});});
        start=true;std::ostringstream out;r.freeze_and_write(out);producer.join();std::ostringstream again;r.freeze_and_write(again);
        check(out.str()==again.str(),"freeze races safely with live producer");
    }
    const std::byte bytes[]={std::byte{1},std::byte{2},std::byte{3}};
    check(fingerprint(bytes)==15035938162879559083ull,"stable pointer-free key fingerprint");
    {
        const auto path=std::filesystem::temp_directory_path()/("wonder-pipeline-trace-"+std::to_string(now_ns())+".csv");
#ifdef _WIN32
        _putenv_s("NX_PIPELINE_MISS_TRACE",path.string().c_str());
#else
        setenv("NX_PIPELINE_MISS_TRACE",path.string().c_str(),1);
#endif
        check(enabled(),"explicit path activates runtime recorder");
        capture(id,Operation::CacheProbe,456,1000297000,true);shutdown();
        const auto read=[&] {std::ifstream f(path);return std::string(std::istreambuf_iterator<char>(f),{});};
        const auto exported=read();
        check(exported.find("cache_probe,456,1,1000297000")!=std::string::npos,"runtime export preserves actual compile-required result");
        capture(id,Operation::SyncCompile,999);shutdown();
        check(read()==exported,"runtime shutdown idempotent and late producer remains stopped");
        std::filesystem::remove(path);
    }
    std::puts("Pipeline miss trace: bounded concurrent retention, freeze, metadata, sanitization and scope passed");
}
