#pragma once
#include <cstdint>
#include <algorithm>
#include <utility>
#include <unordered_set>

namespace nx::nvn {
// Pool maps may still be read by an in-flight replay. Retire the separately
// owned snapshot objects without mutating the retiring pool's descriptor copy.
// Caller holds objects_lock; retire must preserve replay incarnations.
template<class Slots,class Retire>
void retire_descriptor_snapshots(const Slots& snapshots,Retire&& retire) {
    std::unordered_set<uint64_t> seen;
    for(const auto& [slot,key]:snapshots)
        if(key && seen.insert(key).second) retire(key);
}
// A failed/absent new snapshot must invalidate the old key. The caller records
// the old PoolEntry before this mutation, so an older replay keeps its lookup.
template<class Slots,class Retire,class Create>
void replace_descriptor_snapshot(Slots& snapshots,uint32_t slot,Retire&& retire,Create&& create) {
    if(auto it=snapshots.find(slot);it!=snapshots.end()) {
        const auto key=it->second;
        snapshots.erase(it);
        if(key)retire(key);
    }
    if(const uint64_t key=create())snapshots[slot]=key;
}
// Remove one known forward slot from its reverse index, without scanning other
// textures. Metadata alone changes; retiring replay descriptor maps stay intact.
template<class Registry>
void forget_descriptor_slot(Registry& registrations,uint64_t texture,uint64_t pool,uint32_t slot) {
    auto it=registrations.find(texture);
    if(it==registrations.end())return;
    auto& slots=it->second;
    const auto removed=std::pair<uint64_t,uint32_t>{pool,slot};
    slots.erase(std::remove(slots.begin(),slots.end(),removed),slots.end());
    if(slots.empty())registrations.erase(it);
}
template<class Registry,class Slots,class Release>
void forget_descriptor_pool(Registry& registrations,uint64_t pool,const Slots& textures,Release&& release) {
    std::unordered_set<uint64_t> affected;
    for(const auto& [slot,texture]:textures) {
        forget_descriptor_slot(registrations,texture,pool,slot);
        if(texture)affected.insert(texture);
    }
    // A kept texture may occupy multiple slots. Remove every retiring-pool link
    // before checking remaining ownership in other pools.
    for(const auto texture:affected)release(texture);
}

}
