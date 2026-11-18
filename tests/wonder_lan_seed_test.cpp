#include "lan_seed.h"
#include <cstdio>
#include <cstdlib>
#include <set>
static void check(bool ok,const char* text) { if (!ok) { std::fprintf(stderr,"%s\n",text); std::exit(1); } }
int main() {
    namespace seed=nx::wonder::lan_seed;
    std::set<uint32_t> streams;
    for(const auto& site:seed::sites) {
        check(seed::find(site.call+4)==&site,"only the exact RNG return address matches");
        check(!seed::find(site.call) && !seed::find(site.call+8),"nearby timer calls must not match");
        check(seed::verified(site,site.words),"verified version selector");
        auto changed=site.words; changed[3]^=1;
        check(!seed::verified(site,changed) && !seed::verified(site,std::span(site.words).first(7)),"modified or incomplete code fails verification");
        const auto value=seed::value(42,site);
        check(value==seed::value(42,site) && value!=seed::value(43,site),"shared session determines seeds");
        streams.insert(uint32_t(value));
    }
    check(streams.size()==std::size(seed::sites),"tested constructors use distinct low-32-bit random seeds");
    check(!seed::find(0xe16ac) && !seed::find(0xe18f8),"framework timing is never redirected");
    std::puts("Wonder LAN RNG: exact selectors, version rejection, session seeds and timer exclusion passed");
}
