#include "nvn/integer_texture_variants.h"
#include "nvn/nvn_pipeline.h"
#include "nvn/pipeline_graphics_identity.h"
#include <cstdio>
#include <cstdlib>
#include <string_view>
namespace nx {
void log(const char*,...) {}
[[noreturn]] void fatal(const char*,...) {std::abort();}
}
namespace nx::nvn {
const DescriptorBinding* ShaderReflection::find(uint32_t set,uint32_t binding) const {
    for(const auto& b:bindings)if(b.set==set && b.binding==binding)return &b;return nullptr;
}
}
using namespace nx::nvn;
void check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"integer variants: %s\n",why);std::exit(1);}}
void environment(const char* value) {
#ifdef _WIN32
    _putenv_s("NX_INT_TEXTURES",value);
#else
    setenv("NX_INT_TEXTURES",value,1);
#endif
}
void reflection(ShaderInfo& s) {
    s.hash="85896ed5239a16cd";
    for(uint32_t binding:{1,2}) {
        DescriptorBinding b;b.set=2;b.binding=binding;b.type=DescriptorType::CombinedImageSampler;
        b.image.component=ScalarType::Float;s.refl.bindings.push_back(b);
    }
}
int main(int argc,char** argv) {
    const bool disabled=argc==2 && std::string_view(argv[1])=="--disabled";environment(disabled?"0":"1");
    ShaderInfo runtime,warm,signed_shader,fresh,other_order,invalid;
    for(auto* s:{&runtime,&warm,&signed_shader,&fresh,&other_order,&invalid})reflection(*s);
    if(disabled) {
        check(!note_int_texture(&runtime,1,1) && !seed_private_int_texture_variant(&warm,{{1,1}}),"disabled path never installs observations/cache types");
        check(!int_texture_variant_id(&runtime) && !int_texture_variant_id(&warm),"disabled identities remain untyped");return 0;
    }
    check(note_int_texture(&runtime,1,1) && note_int_texture(&signed_shader,1,2),"actual per-shader observations recorded");
    check(int_texture_generation(&runtime)==int_texture_generation(&signed_shader),"reproduce equal ordinal histories");
    check(int_texture_variant_id(&runtime)!=int_texture_variant_id(&signed_shader),"different complete maps never alias equal generation");
    check(int_texture_variant_id(&fresh)==0 && int_texture_kinds(&fresh).empty(),"same hash never inherits actual runtime types");
    check(seed_private_int_texture_variant(&warm,{{1,1}}),"fresh private reflected shader can seed exact observed map");
    check(int_texture_variant_id(&warm)==int_texture_variant_id(&runtime),"private warm and actual runtime semantic identities match");
    check(!seed_private_int_texture_variant(&warm,{{1,2}}),"already installed object cannot be reseeded");
    check(!seed_private_int_texture_variant(&invalid,{{77,1}}) && !int_texture_variant_id(&invalid),"unknown reflected binding rejected without publication");
    check(!seed_private_int_texture_variant(&invalid,{{1,1},{77,1}}) && int_texture_kinds(&invalid).empty(),"valid prefix plus invalid binding never partially seeds");
    check(!seed_private_int_texture_variant(&invalid,{{1,3}}) && !int_texture_variant_id(&invalid),"invalid kind rejected without publication");
    note_int_texture(&runtime,2,2);note_int_texture(&other_order,2,2);note_int_texture(&other_order,1,1);
    check(int_texture_variant_id(&runtime)==int_texture_variant_id(&other_order),"discovery order is absent from semantic identity");
    note_int_texture(&other_order,1,2);note_int_texture(&other_order,1,1);
    check(int_texture_generation(&runtime)!=int_texture_generation(&other_order) &&
          int_texture_variant_id(&runtime)==int_texture_variant_id(&other_order),"different ordinal histories share identical complete map");
    check(int_texture_variant_id(&warm)!=int_texture_variant_id(&runtime),"partial map cannot select later complete variant");
    {
        static_assert(sizeof(PipelineKey)==712,"legacy v1 PipelineKey size");
        PipelineKey draw;draw.vs=&runtime;draw.fs=&signed_shader;
        PipelineKey state;const auto actual=graphics_pipeline_identity(draw,state);
        auto equivalent=draw;equivalent.vs=&other_order;
        check(graphics_pipeline_identity(equivalent,state)==actual,"actual graphics key matches complete VS maps across objects/histories");
        auto changed=draw;changed.vs=&warm;
        check(!(graphics_pipeline_identity(changed,state)==actual),"actual graphics key separates partial VS map");
        changed=draw;changed.fs=&warm;
        check(!(graphics_pipeline_identity(changed,state)==actual),"actual graphics key separates equal-ordinal signed/unsigned FS maps");
        changed=draw;++changed.topology;
        check(!(graphics_pipeline_identity(changed,state)==actual),"render state remains part of actual graphics key");
        auto missing=draw;missing.fs=nullptr;
        check(!(graphics_pipeline_identity(missing,state)==actual),"absent fragment stage remains distinct");
    }
    typed_pipeline::SemanticTokens bounded;
    for(uint32_t i=0;i<bounded.kMaxMaps;++i)bounded.intern({{i,1}});
    const auto old=bounded.intern({{1,1}});bool rejected=false;
    try{bounded.intern({{99999,2}});}catch(const std::length_error&){rejected=true;}
    check(rejected && bounded.size()==bounded.kMaxMaps && bounded.intern({{1,1}})==old,"capacity failure preserves every existing identity and never reuses a token");
    std::puts("Integer texture variants: real observations, private seed validation, semantic identity, disable and bounded failure passed");
}
