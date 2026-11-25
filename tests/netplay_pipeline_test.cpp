#include "netplay_pipeline.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <vector>
static void check(bool ok,const char* text) { if (!ok) { std::fprintf(stderr,"%s\n",text); std::exit(1); } }
int main() {
    using nx::lan::OneAhead;
    std::vector<int> seen;
    {
        OneAhead<int,int> pipeline([&](int job) { seen.push_back(job); return job*3; },[] {});
        check(!pipeline.submit(7),"first frame has an explicit neutral lead-in");
        check(pipeline.submit(8)==21,"next frame receives previous exchange");
        check(pipeline.submit(9)==24,"input order remains exact");
        check(pipeline.receive()==27 && !pipeline.receive(),"drain does not invent a future input");
        check(seen==std::vector<int>({7,8,9}),"each input exchanged once");
    }
    {
        std::promise<void> entered,release;
        auto entered_future=entered.get_future(), release_future=release.get_future();
        OneAhead<int,int> pipeline([&](int job) { entered.set_value(); release_future.wait(); return job; },[&] { release.set_value(); });
        check(!pipeline.submit(1),"submit does not wait for the network exchange");
        check(entered_future.wait_for(std::chrono::seconds(1))==std::future_status::ready,"worker started");
        pipeline.stop();
        bool cancelled=false; try { pipeline.receive(); } catch (...) { cancelled=true; }
        check(cancelled,"shutdown interrupts a blocked exchange and wakes the consumer");
    }
    {
        OneAhead<int,int> pipeline([](int)->int { throw std::runtime_error("desync"); },[] {});
        pipeline.submit(1);
        bool failed=false; try { pipeline.submit(2); } catch (const std::runtime_error& e) { failed=std::string(e.what())=="desync"; }
        check(failed,"transport/desync exceptions reach simulation before another job is queued");
    }
    {
        std::promise<void> entered,release,sampling;
        auto entered_future=entered.get_future(),release_future=release.get_future(),sampling_future=sampling.get_future();
        std::atomic<int> physical_input{1};
        OneAhead<int,int> pipeline([&](int job) {
            if(job==1) {entered.set_value();release_future.wait();}
            return job;
        },[] {});
        check(!pipeline.submit_sampled([&]{return physical_input.load();}),"sampled input retains the neutral lead-in");
        entered_future.wait();
        auto producer=std::async(std::launch::async,[&]{return pipeline.submit_sampled([&]{sampling.set_value();return physical_input.load();});});
        check(sampling_future.wait_for(std::chrono::milliseconds(50))==std::future_status::timeout,"controller poll waits for the outstanding exchange");
        physical_input=2;release.set_value();
        check(producer.get()==1 && pipeline.receive()==2,"input changed during a network stall reaches the next committed frame");
    }
    {
        OneAhead<int,int> pipeline([](int)->int {throw std::runtime_error("failed peer");},[]{});
        pipeline.submit_sampled([]{return 1;});
        bool sampled=false,failed=false;
        try {pipeline.submit_sampled([&]{sampled=true;return 2;});}
        catch(const std::runtime_error& error){failed=std::string(error.what())=="failed peer";}
        check(failed && !sampled,"failed exchanges reach the caller before polling or queuing another input");
    }
    std::puts("LAN one-frame pipeline: overlap, input order, drain, cancellation and failure passed");
}
