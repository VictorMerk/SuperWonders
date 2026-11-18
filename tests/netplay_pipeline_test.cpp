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
    std::puts("LAN one-frame pipeline: overlap, input order, drain, cancellation and failure passed");
}
