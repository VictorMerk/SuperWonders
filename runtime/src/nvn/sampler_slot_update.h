#pragma once
namespace nx::nvn {
// Notify before overwriting so in-flight replays retain the preceding state.
// Identical re-registration neither touches the map value nor records history.
template<class Map,class Equal,class Before>
bool update_sampler_slot(Map& slots,const typename Map::key_type& id,const typename Map::mapped_type& value,
                         Equal equal,Before before) {
    auto it=slots.find(id);
    if(it!=slots.end() && equal(it->second,value)) return false;
    before(it==slots.end() ? nullptr : &it->second);
    if(it==slots.end()) slots.emplace(id,value);
    else it->second=value;
    return true;
}
}
