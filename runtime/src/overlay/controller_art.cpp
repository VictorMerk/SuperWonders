#include "overlay/controller_art.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 1024
#include "stb_image.h"
namespace nx::overlay {
namespace {
#include "controller_art_data.inc"
struct Art {const unsigned char* data;int bytes;int rect=-1,width=0,height=0;};
Art art[]={
 {art_dualsense,sizeof art_dualsense},{art_dualshock4,sizeof art_dualshock4},
 {art_xbox_series,sizeof art_xbox_series},{art_xbox_one,sizeof art_xbox_one},{art_xbox360,sizeof art_xbox360},
 {art_switch_pro,sizeof art_switch_pro},{art_joycon_pair,sizeof art_joycon_pair},
 {art_steam_controller,sizeof art_steam_controller},{art_steam_deck,sizeof art_steam_deck}};
static_assert(std::size(art)==kControllerArtCount);
ImFontAtlas* art_atlas=nullptr;
int index(ControllerArtModel model){return std::clamp(int(model),0,kControllerArtCount-1);}
}
const char* controller_art_name(ControllerArtModel model){
 static const char* names[]={"DualSense (PS5)","DualShock 4 (PS4)","Xbox Series","Xbox One","Xbox 360","Nintendo Switch Pro","Paired Joy-Cons","Steam Controller","Steam Deck"};return names[index(model)];
}
ControllerArtModel detect_controller_art(std::string_view name,plat::ControllerKind fallback){
 std::string n(name);std::transform(n.begin(),n.end(),n.begin(),[](unsigned char c){return char(std::tolower(c));});
 auto has=[&](const char* s){return n.find(s)!=std::string::npos;};
 if(has("steam deck")||has("steamdeck"))return ControllerArtModel::SteamDeck;
 if(has("steam controller"))return ControllerArtModel::SteamController;
 if(has("dualsense")||has("ps5"))return ControllerArtModel::DualSense;
 if(has("dualshock")||has("ps4"))return ControllerArtModel::DualShock4;
 if(has("joy-con")||has("joycon"))return ControllerArtModel::JoyConPair;
 if(has("switch")||has("nintendo"))return ControllerArtModel::SwitchPro;
 if(has("360"))return ControllerArtModel::Xbox360;
 if(has("xbox one"))return ControllerArtModel::XboxOne;
 if(fallback==plat::ControllerKind::PlayStation)return ControllerArtModel::DualSense;
 if(fallback==plat::ControllerKind::Nintendo)return ControllerArtModel::SwitchPro;
 return ControllerArtModel::XboxSeries;
}
const ControllerArtLayout& controller_art_layout(ControllerArtModel model){
 using K=plat::ControllerKind;
 // South/east/west/north, shoulders, triggers, start/back, stick clicks, d-pad.
 static const ControllerArtLayout layouts[]={
 {K::PlayStation,{{{.82f,.635f},{.895f,.525f},{.745f,.525f},{.82f,.415f},{.18f,.18f},{.82f,.18f},{.18f,.065f},{.82f,.065f},{.738f,.35f},{.255f,.35f},{.34f,.78f},{.66f,.78f},{.115f,.53f},{.18f,.445f},{.245f,.53f},{.18f,.615f}}}},
 {K::PlayStation,{{{.80f,.63f},{.875f,.51f},{.73f,.51f},{.80f,.39f},{.20f,.14f},{.80f,.14f},{.20f,.065f},{.80f,.065f},{.70f,.34f},{.30f,.34f},{.34f,.77f},{.66f,.77f},{.15f,.51f},{.20f,.42f},{.25f,.51f},{.20f,.60f}}}},
 {K::Xbox,{{{.77f,.53f},{.845f,.42f},{.70f,.42f},{.77f,.32f},{.22f,.15f},{.78f,.15f},{.22f,.055f},{.78f,.055f},{.58f,.43f},{.43f,.43f},{.22f,.46f},{.64f,.71f},{.31f,.70f},{.36f,.62f},{.41f,.70f},{.36f,.78f}}}},
 {K::Xbox,{{{.77f,.61f},{.845f,.51f},{.70f,.51f},{.77f,.41f},{.22f,.19f},{.78f,.19f},{.22f,.055f},{.78f,.055f},{.575f,.52f},{.415f,.52f},{.22f,.53f},{.63f,.77f},{.30f,.75f},{.355f,.66f},{.41f,.75f},{.355f,.84f}}}},
 {K::Xbox,{{{.80f,.61f},{.885f,.50f},{.72f,.51f},{.81f,.40f},{.23f,.16f},{.77f,.16f},{.22f,.065f},{.78f,.065f},{.615f,.51f},{.385f,.51f},{.19f,.54f},{.64f,.78f},{.28f,.76f},{.34f,.67f},{.40f,.76f},{.34f,.85f}}}},
 {K::Nintendo,{{{.76f,.42f},{.84f,.33f},{.68f,.33f},{.76f,.24f},{.20f,.07f},{.80f,.07f},{.20f,.02f},{.80f,.02f},{.62f,.23f},{.37f,.23f},{.22f,.33f},{.63f,.52f},{.30f,.52f},{.345f,.46f},{.39f,.52f},{.345f,.58f}}},true},
 {K::Nintendo,{{{.80f,.31f},{.875f,.235f},{.725f,.235f},{.80f,.16f},{.20f,.06f},{.80f,.06f},{.20f,.02f},{.80f,.02f},{.71f,.07f},{.38f,.07f},{.21f,.24f},{.80f,.515f},{.14f,.515f},{.21f,.44f},{.28f,.515f},{.21f,.59f}}},true},
 {K::Xbox,{{{.61f,.58f},{.665f,.50f},{.555f,.50f},{.61f,.42f},{.20f,.05f},{.80f,.05f},{.20f,.02f},{.80f,.02f},{.58f,.30f},{.42f,.30f},{.385f,.50f},{.79f,.30f},{.145f,.30f},{.22f,.20f},{.30f,.30f},{.22f,.40f}}},true},
 {K::Xbox,{{{.93f,.26f},{.96f,.19f},{.90f,.19f},{.93f,.12f},{.15f,.05f},{.85f,.05f},{.15f,.02f},{.85f,.02f},{.88f,.06f},{.11f,.06f},{.16f,.24f},{.83f,.24f},{.035f,.18f},{.06f,.12f},{.085f,.18f},{.06f,.24f}}},true}};
 return layouts[index(model)];
}
void init_controller_art(ImFontAtlas* atlas){
 art_atlas=atlas;
 atlas->TexDesiredWidth=std::max(atlas->TexDesiredWidth,2048);
 for(auto& a:art){a.rect=-1;int components=0;if(stbi_info_from_memory(a.data,a.bytes,&a.width,&a.height,&components)&&a.width<=640&&a.height<=440)a.rect=atlas->AddCustomRectRegular(a.width+2,a.height+2);}
 unsigned char* pixels=nullptr;int width=0,height=0;atlas->GetTexDataAsRGBA32(&pixels,&width,&height);
 if(!pixels){art_atlas=nullptr;return;}
 atlas->TexPixelsUseColors=true;
 for(auto& a:art)if(a.rect>=0){int w=0,h=0,components=0;auto* decoded=stbi_load_from_memory(a.data,a.bytes,&w,&h,&components,4);
  if(!decoded||w!=a.width||h!=a.height){a.rect=-1;stbi_image_free(decoded);continue;}
  const auto* rect=atlas->GetCustomRectByIndex(a.rect);
  if(!rect->IsPacked()){a.rect=-1;stbi_image_free(decoded);continue;}
  for(int y=0;y<h;++y)std::memcpy(pixels+((rect->Y+1+y)*width+rect->X+1)*4,decoded+y*w*4,w*4);
  stbi_image_free(decoded);
 }
}
float controller_art_aspect(ControllerArtModel model){const auto& a=art[index(model)];return a.height>0?float(a.width)/a.height:1.6f;}
bool draw_controller_art(ControllerArtModel model,ImDrawList* draw,ImVec2 p,ImVec2 q,ImU32 tint){
 const auto& a=art[index(model)];if(art_atlas!=ImGui::GetIO().Fonts||a.rect<0||!art_atlas->TexPixelsRGBA32)return false;
 const auto* rect=art_atlas->GetCustomRectByIndex(a.rect);
 ImVec2 uv0{float(rect->X+1)/art_atlas->TexWidth,float(rect->Y+1)/art_atlas->TexHeight};
 ImVec2 uv1{float(rect->X+1+a.width)/art_atlas->TexWidth,float(rect->Y+1+a.height)/art_atlas->TexHeight};
 draw->AddImage(art_atlas->TexID,p,q,uv0,uv1,tint);return true;
}
}
