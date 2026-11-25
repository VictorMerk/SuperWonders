#include "nvn/descriptor_snapshot_lifetime.h"
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <vector>
using namespace nx::nvn;
static void check(bool ok,const char* why) { if(!ok) { std::cerr << why << '\n';std::exit(1); } }
int main() {
    using Slots=std::map<uint32_t,uint64_t>;
    // Model separately registered snapshots and the deferred objects held by a
    // replay. A descriptor-pool destructor alone cannot release this registry.
    std::map<uint64_t,std::shared_ptr<int>> registry;
    std::vector<std::shared_ptr<int>> deferred;
    std::map<uint64_t,unsigned> retire_counts;
    const auto retire=[&](uint64_t key) {
        ++retire_counts[key];
        if(auto it=registry.find(key);it!=registry.end()) {
            deferred.push_back(std::move(it->second)); registry.erase(it);
        }
    };
    registry[100]=std::make_shared<int>(7); registry[200]=std::make_shared<int>(9);
    const auto old_image=std::weak_ptr<int>(registry[100]);
    Slots old_pool{{1,100},{2,200},{3,100},{4,0}};
    const auto preserved=old_pool;
    retire_descriptor_snapshots(old_pool,retire);
    check(registry.empty(),"finalized pool must release all separately registered snapshots");
    check(old_pool==preserved,"retired pool map must remain intact for older replay lookup");
    check(retire_counts[100]==1 && retire_counts[200]==1 && !retire_counts.count(0),"duplicate snapshot keys retire once; null ownership skipped");
    check(!old_image.expired() && *deferred[0]==7,"image remains alive while replay retirement is deferred");
    Slots new_pool{{1,300}}; registry[300]=std::make_shared<int>(11);
    check(old_pool.at(1)==100 && new_pool.at(1)==300,"pool reuse preserves old and current descriptor identities independently");
    replace_descriptor_snapshot(new_pool,1,retire,[] { return uint64_t(0); });
    check(!new_pool.count(1) && !registry.count(300),"failed new snapshot invalidates previous slot ownership");
    registry[400]=std::make_shared<int>(13);
    replace_descriptor_snapshot(new_pool,1,retire,[] { return uint64_t(400); });
    check(new_pool.at(1)==400 && retire_counts[300]==1,"successful replacement publishes only the new snapshot");
    deferred.clear(); check(old_image.expired(),"replay completion releases retired images");
    retire_descriptor_snapshots(new_pool,retire); deferred.clear();
    for(unsigned cycle=0;cycle<1000;++cycle) {
        Slots pool;
        for(unsigned slot=0;slot<16;++slot) {
            const uint64_t key=1000+uint64_t(cycle)*16+slot;
            registry[key]=std::make_shared<int>(slot); pool[slot]=key;
        }
        retire_descriptor_snapshots(pool,retire); deferred.clear();
        check(registry.empty(),"repeated finalize/reinitialize must not accumulate snapshot registry objects");
    }
    using Reverse=std::map<uint64_t,std::vector<std::pair<uint64_t,uint32_t>>>;
    Reverse reverse{{10,{{5,1},{5,2},{6,3}}},{20,{{5,4}}},{30,{{9,1}}}};
    const Slots retiring{{1,10},{2,10},{4,20}};
    std::map<uint64_t,unsigned> release_checks;
    forget_descriptor_pool(reverse,5,retiring,[&](uint64_t key) {
        ++release_checks[key];
        if(key==10)check(reverse.at(10)==Reverse::mapped_type{{6,3}},"other pool ownership must survive");
        if(key==20)check(!reverse.count(20),"last reverse entry must be removed before release check");
    });
    check(release_checks[10]==1 && release_checks[20]==1,"each affected texture checked once after all pool slots removed");
    check(retiring==Slots{{1,10},{2,10},{4,20}},"reverse cleanup must not mutate old replay descriptors");
    check(reverse.at(30)==Reverse::mapped_type{{9,1}},"unrelated texture metadata unchanged");
    forget_descriptor_slot(reverse,10,6,3);
    check(!reverse.count(10),"slot replacement removes empty reverse bucket");
    forget_descriptor_slot(reverse,999,123,0); // missing/finalized source is safe
    reverse.clear();
    for(unsigned cycle=0;cycle<1000;++cycle) {
        const uint64_t old_texture=10000+cycle;
        reverse[old_texture].push_back({55,7});
        // Slot replacement under a stable live pool; no historical entries remain.
        forget_descriptor_slot(reverse,old_texture,55,7);
        check(reverse.empty(),"slot churn must not accumulate old texture reverse metadata");
        reverse[old_texture]={{55,1},{55,2}};
        forget_descriptor_pool(reverse,55,Slots{{1,old_texture},{2,old_texture}},[](uint64_t) {});
        check(reverse.empty(),"pool address reuse must not accumulate reverse metadata");
        reverse[old_texture]={{55,1}};
        reverse.erase(old_texture); // default KEEP_TEXTURES-off finalize/reinitialize path
        check(reverse.empty(),"finalized guest reverse metadata must be forgotten");
    }
    std::cout << "Descriptor snapshot lifetime: deduplicated cleanup, replay preservation, slot invalidation and 1000 pool-reuse cycles passed\n";
}
