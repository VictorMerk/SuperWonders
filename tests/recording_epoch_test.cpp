#include "nvn/nvn.h"
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <thread>

// Run the actual incarnation implementation with an in-memory registry only.
// No Vulkan instance, GPU queue or guest/game image is created.
namespace nx { void log(const char*, ...) {} }
namespace nx::nvn {
ObjectsLock& objects_lock() {static ObjectsLock lock;return lock;}
std::unordered_map<uint64_t,std::unique_ptr<Object>>& objects() {
    static std::unordered_map<uint64_t,std::unique_ptr<Object>> registry;return registry;
}
void drain_objects() {}
}
namespace {
void check(bool ok,const char* text) {if(!ok){std::cerr<<text<<'\n';std::exit(1);}}
struct Value: nx::nvn::Object {int value;explicit Value(int v):value(v){}};
}
int main() {
    using namespace nx::nvn;
    using namespace std::chrono_literals;
    constexpr uint64_t pool=100,snapshot=200;
    objects()[pool]=std::make_unique<Value>(1);
    objects()[snapshot]=std::make_unique<Value>(2);
    auto* old_pool=objects()[pool].get();auto* old_snapshot=objects()[snapshot].get();
    const auto before=recording_epoch();
    std::vector<std::unique_ptr<Object>> retired;
    std::promise<void> halfway,finish;
    auto halfway_ready=halfway.get_future();auto finish_ready=finish.get_future();
    uint64_t middle=0,after=0;
    std::thread mutate([&] {
        std::lock_guard lock(objects_lock());
        note_replaced(snapshot);
        retired.push_back(std::move(objects()[snapshot]));objects().erase(snapshot);
        middle=current_epoch();halfway.set_value();finish_ready.wait();
        note_replaced(pool);retired.push_back(std::move(objects()[pool]));
        objects()[pool]=std::make_unique<Value>(3);after=current_epoch();
    });
    halfway_ready.wait();
    std::promise<void> observing;
    auto observing_ready=observing.get_future();
    auto capture=std::async(std::launch::async,[&] {observing.set_value();return recording_epoch();});
    observing_ready.wait();
    check(capture.wait_for(30ms)==std::future_status::timeout,"recording mark must wait for the descriptor transaction");
    finish.set_value();mutate.join();
    const auto mark=capture.get();
    check(mark==after&&mark>middle,"end-recording gets complete transaction epoch");
    {
        std::shared_lock lock(objects_lock());
        check(find_object(pool,before)==old_pool&&find_object(snapshot,before)==old_snapshot,"older replay resolves both original objects");
        check(find_object(pool,middle)==old_pool&&find_object(snapshot,middle)==nullptr,"unlocked intermediate epoch reproduces old pool with missing snapshot");
        check(find_object(pool,mark)==objects().at(pool).get(),"coherent new mark resolves new pool");
    }
    purge_incarnations(UINT64_MAX);
    std::cout<<"Actual incarnation regression: end-recording cannot capture a partial descriptor retirement\n";
}
