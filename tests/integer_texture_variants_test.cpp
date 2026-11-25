#include "nvn/integer_texture_variants.h"
#include <cstdio>
#include <cstdlib>

using namespace nx::nvn;
void check(bool ok, const char* why) {
    if (!ok) {
        std::fprintf(stderr, "%s\n", why);
        std::abort();
    }
}

int main() {
    typed_pipeline::SemanticTokens tokens;
    check(tokens.intern({{1, 1}}) == tokens.intern({{1, 1}}), "equal binding maps share a token");
    check(tokens.intern({{1, 1}}) != tokens.intern({{1, 2}}), "signed and unsigned kinds differ");
    typed_pipeline::SemanticTokens bounded;
    for(uint32_t i=0;i<bounded.kMaxMaps;++i)bounded.intern({{i,1}});
    const auto old=bounded.intern({{1,1}});bool rejected=false;
    try{bounded.intern({{99999,2}});}catch(const std::length_error&){rejected=true;}
    check(rejected && bounded.size()==bounded.kMaxMaps && bounded.intern({{1,1}})==old,"capacity failure preserves every existing identity and never reuses a token");
}
