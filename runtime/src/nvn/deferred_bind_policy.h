#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <utility>
namespace nx::nvn::deferred_bind {
struct Request {int32_t stage=0,first=0,count=0;uint64_t address=0;};
constexpr bool enabled(uint32_t kind,bool global,bool textures_only) {
    return kind<4 && (global || (kind==0 && textures_only));
}
// Actual dispatch used by the HLE wrapper: ordinary forms copy at recording;
// deferred forms retain the array address until the selected submit/read point.
template<class Ordinary,class Deferred>
void dispatch(uint32_t kind,const Request& request,bool global,bool textures_only,Ordinary&& ordinary,Deferred&& deferred) {
    if(!enabled(kind,global,textures_only)){std::forward<Ordinary>(ordinary)();return;}
    if(request.address && request.count>0)std::forward<Deferred>(deferred)(request);
}
constexpr uint32_t required_words(uint32_t kind,uint32_t count) {
    return kind<4 && count<=64 ? count*(kind>=2?2u:1u) : 0;
}
template<class Spans>
void note_span(Spans& spans,uint64_t address,uint32_t kind,uint32_t count) {
    const auto words=required_words(kind,count);
    if(!address || !words)return;
    auto& span=spans[address];if(words>span)span=words;
}
template<class Read>
std::vector<uint64_t> capture_span(uint32_t words,Read&& read) {
    if(words>128)return {};
    std::vector<uint64_t> values(words);
    for(std::size_t k=0;k<values.size();++k)values[k]=read(k);
    return values;
}
template<class Read>
std::vector<uint64_t> capture_words(uint32_t kind,uint32_t count,Read&& read) {
    return capture_span(required_words(kind,count),std::forward<Read>(read));
}
} // namespace nx::nvn::deferred_bind
