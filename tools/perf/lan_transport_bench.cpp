#include "netplay_session.h"
#include "netplay_pipeline.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <future>
#include <mutex>
#include <thread>
using Clock=std::chrono::steady_clock;
int main(int argc,char** argv) {
    if(argc!=6) return 2;
    const uint16_t proxy=uint16_t(std::stoi(argv[1]));
    const int work=std::stoi(argv[3]), frames=std::stoi(argv[4]);
    const bool pipelined=std::stoi(argv[5])!=0;
    nx::lan::SessionOptions config;
    config.port=0; config.players=2; config.key=nx::lan::pairing_key("private-latency-experiment");
    config.build[0]=42; config.save[0]=7; config.epoch=1700000000; config.seed=42;
    config.timeout=std::chrono::seconds(10);
    std::promise<uint16_t> listening; auto ready=listening.get_future();
    std::mutex lock; bool failed=false; double elapsed[2]{}, measured_work[2]{};
    auto run=[&](int role) {
        try {
            nx::lan::Session session;
            if(role==0) session.host(config,[&](uint16_t port) {
                std::ofstream out(argv[2]); out<<port; out.close(); listening.set_value(port);
            });
            else { auto guest=config; guest.port=proxy; session.join(guest); }
            struct Job { nx::plat::PadState pad; uint64_t hash; };
            nx::lan::OneAhead<Job,nx::lan::FrameInputs> pipeline([&](Job job) { return session.exchange(job.pad,job.hash); },[&] { session.stop(); });
            const auto start=Clock::now();
            auto verify=[&](const nx::lan::FrameInputs& frame,int tick) {
                if(frame.tick!=uint64_t(tick) || frame.players!=2 || frame.pads[0].buttons!=1 || frame.pads[1].buttons!=2 ||
                   frame.pads[0].lx!=tick || frame.pads[1].lx!=tick) throw std::runtime_error("Input/tick mismatch");
            };
            for(int tick=0;tick<frames;++tick) {
                const auto work_start=Clock::now();
                std::this_thread::sleep_for(std::chrono::milliseconds(work));
                measured_work[role]+=std::chrono::duration<double,std::milli>(Clock::now()-work_start).count()/frames;
                nx::plat::PadState pad{uint64_t(role+1),tick,-tick,0,0};
                if(pipelined) {
                    const auto previous=pipeline.submit({pad,uint64_t(tick+1)});
                    if(previous) verify(*previous,tick-1);
                } else verify(session.exchange(pad,uint64_t(tick+1)),tick);
            }
            if(pipelined) verify(*pipeline.receive(),frames-1);
            elapsed[role]=std::chrono::duration<double,std::milli>(Clock::now()-start).count()/frames;
        } catch(const std::exception& error) {
            std::lock_guard guard(lock); std::fprintf(stderr,"role %d: %s\n",role,error.what()); failed=true;
        }
    };
    std::thread host([&]{run(0);});
    if(ready.wait_for(std::chrono::seconds(5))!=std::future_status::ready) {host.join(); return 2;}
    std::thread guest([&]{run(1);}); guest.join(); host.join();
    std::printf("{\"frames\":%d,\"work_ms\":%d,\"host_ms_per_tick\":%.3f,\"guest_ms_per_tick\":%.3f,\"measured_host_work_ms\":%.3f,\"measured_guest_work_ms\":%.3f,\"inputs_verified\":%s}\n",
        frames,work,elapsed[0],elapsed[1],measured_work[0],measured_work[1],failed?"false":"true");
    return failed?1:0;
}
