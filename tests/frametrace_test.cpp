#include "frametrace.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

static uint64_t now = 0;
static FILE* captured = nullptr;
static unsigned clock_reads = 0;
namespace nx::plat {
uint64_t monotonic_ns() { ++clock_reads; return now; }
FILE* fopen_utf8(const std::string&,const char*) { return captured = std::tmpfile(); }
}
void check(bool value,const char* message) {
    if (!value) { std::fprintf(stderr,"%s\n",message); std::exit(1); }
}
std::vector<std::string> columns(const std::string& line) {
    std::vector<std::string> out; std::istringstream in(line); std::string item;
    while (std::getline(in,item,',')) {
        while (!item.empty() && (item.back()=='\r' || item.back()=='\n')) item.pop_back();
        out.push_back(item);
    }
    return out;
}
int main(int argc,char**) {
    if(argc>1) {
#ifdef _WIN32
        _putenv_s("NX_FRAMETRACE","");
#else
        unsetenv("NX_FRAMETRACE");
#endif
        for(unsigned phase=0;phase<unsigned(nx::ft::TimedCall::Count);++phase) {
            nx::ft::CallTimer timer(static_cast<nx::ft::TimedCall>(phase));
            timer.finish();timer.finish();
        }
        check(clock_reads==0,"disabled total/phase timers read no clocks, including explicit finish");
        std::puts("disabled frame-trace timers read no clocks");return 0;
    }
#ifdef _WIN32
    _putenv_s("NX_FRAMETRACE","synthetic-trace");
#else
    setenv("NX_FRAMETRACE","synthetic-trace",1);
#endif
    using namespace nx::ft;
    now = 100000000; input_sample(7);
    now = 102000000; input_sample(7);
    check(input_sample_time(7)==100000000,"repeat HID reads retain the first sample");
    now = 103000000; input_sample(8);
    check(input_sample_time(7)==100000000 && input_sample_time(9)==0,"frame IDs remain distinct");
    input_sample_at(9,90000000);
    check(input_sample_time(9)==90000000,"buffered inputs retain their real sampling age");
    input_sample_at(9,0);
    check(input_sample_time(9)==90000000,"neutral lead-in does not invent a sample timestamp");
    now = 104000000;
    { CallTimer timer(TimedCall::Logging);
      { CallTimer wait(TimedCall::LogMutexWait);now=104250000;wait.finish();
        const auto reads=clock_reads;now=104500000;wait.finish();
        check(clock_reads==reads,"finish is idempotent and reads no extra clock"); }
      { CallTimer platform(TimedCall::LogPlatformSink);now=105500000; }
      { CallTimer file(TimedCall::LogFileSink);now=106000000; }
    }
    { CallTimer timer(TimedCall::Logging); {CallTimer setup(TimedCall::LogFileSetup);now=106750000;} now = 107000000; }
    { CallTimer timer(TimedCall::InputPoll); now = 109000000; }
    now = 110000000;
    host_present(1000,2000,3000,4000,input_sample_time(7),true);
    now = 115000000;
    host_present(1000,2000,4000,5000,input_sample_time(8),false,2000000,52000000);
    check(captured!=nullptr,"presentation produces the requested trace");
    std::fflush(captured); std::rewind(captured);
    char line[4096];
    check(std::fgets(line,sizeof line,captured)!=nullptr,"CSV header");
    const auto header = columns(line);
    check(std::find(header.begin(),header.end(),"sample_to_present_ms")!=header.end(),"latency column is named as a proxy");
    check(std::fgets(line,sizeof line,captured)!=nullptr,"original picture row");
    auto row = columns(line);
    auto value = [&](const char* name) { auto at=std::find(header.begin(),header.end(),name); check(at!=header.end(),"diagnostic column exists"); return std::stod(row[size_t(at-header.begin())]); };
    check(value("log_ms")==3 && value("log_max_ms")==2 && value("input_poll_ms")==2 && value("input_poll_max_ms")==2,
          "completed calls retain distinct summed and longest durations");
    check(value("log_mutex_ms")==0.25&&value("log_mutex_max_ms")==0.25&&
          value("log_platform_ms")==1&&value("log_platform_max_ms")==1&&
          value("log_file_ms")==0.5&&value("log_setup_ms")==0.75,
          "phase totals/maxima separate completed wait/sinks/setup within total calls");
    check(row.size()==header.size() && value("sample_to_present_ms")==10.0 && value("original_picture")==1,
          "original picture retains the matching 10 ms sample");
    check(std::fgets(line,sizeof line,captured)!=nullptr,"generated picture row");
    row = columns(line);
    check(value("log_ms")==0 && value("log_max_ms")==0 && value("input_poll_ms")==0 && value("input_poll_max_ms")==0,
          "new presentation intervals do not inherit old call maxima");
    check(value("log_mutex_ms")==0&&value("log_mutex_max_ms")==0&&value("log_platform_max_ms")==0&&
          value("log_file_ms")==0&&value("log_setup_max_ms")==0,"phase maxima reset with presentation interval");
    check(row.size()==header.size() && value("sample_to_present_ms")==12.0 && value("original_picture")==0,
          "generated picture is explicitly labelled");
    check(value("present_pace_requested_ms")==2.0 && value("present_pace_actual_ms")==52.0,
          "presentation sleep retains requested and actual wall durations");
    input_sample(7+128);
    check(input_sample_time(7)==0 && input_sample_time(7+128)==now,"wrapped samples never alias an old frame");
    std::puts("frame trace sample/presentation association passed");
}
