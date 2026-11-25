#pragma once
#include <string_view>
namespace nx::play {
// StageLoadInfo can stay cached across course exits. The map's scene resource
// is reopened, so it also invalidates any player pointer from the old course.
inline std::string_view map_scene_resource(std::string_view path) {
    constexpr std::string_view marker="/Bake/Scene/";
    const auto at=path.find(marker);
    if(at==path.npos)return {};
    const auto name=path.substr(at+marker.size());
    constexpr std::string_view suffix=".bkres.zs";
    if(name.size()!=8+suffix.size() || name.substr(0,5)!="World" || name.substr(8)!=suffix)return {};
    for(auto c:name.substr(5,3))if(c<'0'||c>'9')return {};
    return name.substr(0,8);
}
}
