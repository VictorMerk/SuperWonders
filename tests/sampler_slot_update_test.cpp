#include "sampler_slot_update.h"
#include <map>
#include <vector>
#include <cstdlib>
#include <cstdio>
static void check(bool ok) { if(!ok) std::abort(); }
struct Value {
    int state=0;
    static inline int assignments=0;
    Value& operator=(const Value& other) { state=other.state;++assignments;return *this; }
};
int main() {
    std::map<unsigned,Value> slots;std::vector<int> history;
    auto equal=[](const Value& a,const Value& b) { return a.state==b.state; };
    auto before=[&](const Value* old) {
        if(old) check(slots.at(7).state==old->state);
        else check(!slots.contains(7));
        history.push_back(old ? old->state : -1);
    };
    check(nx::nvn::update_sampler_slot(slots,7u,Value{3},equal,before));
    const auto assignments=Value::assignments;
    for(int i=0;i<1000;++i) check(!nx::nvn::update_sampler_slot(slots,7u,Value{3},equal,before));
    check(Value::assignments==assignments && history==std::vector<int>{-1});
    check(nx::nvn::update_sampler_slot(slots,7u,Value{9},equal,before));
    check(slots.at(7).state==9 && history==std::vector<int>({-1,3}) && Value::assignments==assignments+1);
    std::puts("sampler registration: identical updates avoid writes/history; changed state records its predecessor first");
}
