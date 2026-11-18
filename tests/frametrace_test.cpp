#include "frametrace.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

static uint64_t now = 0;
static FILE* captured = nullptr;
namespace nx::plat {
uint64_t monotonic_ns() { return now; }
FILE* fopen_utf8(const std::string&,const char*) { return captured = std::tmpfile(); }
}
void check(bool value,const char* message) {
    if (!value) { std::fprintf(stderr,"%s\n",message); std::exit(1); }
}
std::vector<std::string> columns(const std::string& line) {
    std::vector<std::string> out; std::istringstream in(line); std::string item;
    while (std::getline(in,item,',')) out.push_back(item);
    return out;
}
int main() {
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
    { CallTimer timer(TimedCall::Logging); now = 106000000; }
    { CallTimer timer(TimedCall::Logging); now = 107000000; }
    { CallTimer timer(TimedCall::InputPoll); now = 109000000; }
    now = 110000000;
    host_present(1000,2000,3000,4000,input_sample_time(7),true);
    now = 115000000;
    host_present(1000,2000,4000,5000,input_sample_time(8),false);
    check(captured!=nullptr,"presentation produces the requested trace");
    std::fflush(captured); std::rewind(captured);
    char line[4096];
    check(std::fgets(line,sizeof line,captured)!=nullptr,"CSV header");
    const auto header = columns(line);
    check(header[header.size()-2]=="sample_to_present_ms","latency column is named as a proxy");
    check(std::fgets(line,sizeof line,captured)!=nullptr,"original picture row");
    auto row = columns(line);
    auto value = [&](const char* name) { auto at=std::find(header.begin(),header.end(),name); check(at!=header.end(),"diagnostic column exists"); return std::stod(row[size_t(at-header.begin())]); };
    check(value("log_ms")==3 && value("log_max_ms")==2 && value("input_poll_ms")==2 && value("input_poll_max_ms")==2,
          "completed calls retain distinct summed and longest durations");
    check(row.size()==header.size() && std::stod(row[row.size()-2])==10.0 && std::stoi(row.back())==1,
          "original picture retains the matching 10 ms sample");
    check(std::fgets(line,sizeof line,captured)!=nullptr,"generated picture row");
    row = columns(line);
    check(value("log_ms")==0 && value("log_max_ms")==0 && value("input_poll_ms")==0 && value("input_poll_max_ms")==0,
          "new presentation intervals do not inherit old call maxima");
    check(row.size()==header.size() && std::stod(row[row.size()-2])==12.0 && std::stoi(row.back())==0,
          "generated picture is explicitly labelled");
    input_sample(7+128);
    check(input_sample_time(7)==0 && input_sample_time(7+128)==now,"wrapped samples never alias an old frame");
    std::puts("frame trace sample/presentation association passed");
}
