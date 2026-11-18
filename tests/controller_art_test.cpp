#include "overlay/controller_remapping.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <chrono>
using namespace nx;
namespace {std::array<bool,16> held{};overlay::Settings* active=nullptr;int changes=0;}
namespace nx::plat {
ControllerKind controller_kind(uint32_t){return ControllerKind::Xbox;}
std::string controller_model_hint(uint32_t){return {};}
std::string controller_name(uint32_t){return active&&active->controller_artwork?std::string("Preview: ")+overlay::controller_art_name(overlay::ControllerArtModel(active->controller_artwork-1)):"Xbox Series Controller";}
bool read_controller(uint32_t,PadState& out,std::array<bool,16>* physical){out={};if(physical)*physical=held;return true;}
}
namespace nx::overlay {
void settings_changed(bool){++changes;}
uint64_t pad_buttons(){return 0;}
namespace ui {
const Theme& theme(){static Theme t={"Test",IM_COL32(255,204,72,255),IM_COL32(105,222,182,255),IM_COL32(251,247,230,255),IM_COL32(200,216,210,255)};return t;}
ImU32 fade(ImU32 c,float alpha){return ImGui::GetColorU32(c,alpha);}
}
}
static void check(bool pass,const char* what){if(!pass){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
static void dump(const std::string& prefix,int width,int height){
 const auto* data=ImGui::GetDrawData();FILE* f=std::fopen((prefix+".mesh").c_str(),"wb");check(f,"mesh file");
 for(const auto* list:data->CmdLists)for(const auto& cmd:list->CmdBuffer)for(unsigned int i=0;i+2<cmd.ElemCount;i+=3){float clip[]={cmd.ClipRect.x,cmd.ClipRect.y,cmd.ClipRect.z,cmd.ClipRect.w};std::fwrite(clip,4,4,f);for(int n=0;n<3;++n){
  const auto& v=list->VtxBuffer[list->IdxBuffer[cmd.IdxOffset+i+n]+cmd.VtxOffset];float values[]={v.pos.x,v.pos.y,v.uv.x,v.uv.y};std::fwrite(values,4,4,f);std::fwrite(&v.col,4,1,f);
 }}
 std::fclose(f);std::ofstream(prefix+".size")<<width<<' '<<height;
}
int main(int argc,char** argv){
 using namespace overlay;using K=plat::ControllerKind;using M=ControllerArtModel;
 check(detect_controller_art("Sony DUALSENSE Wireless",K::Xbox)==M::DualSense,"DualSense name takes precedence over generic driver");
 check(detect_controller_art("PS4 Controller",K::Xbox)==M::DualShock4,"PS4");
 check(detect_controller_art("Xbox One S",K::Xbox)==M::XboxOne,"Xbox One");
 check(detect_controller_art("Xbox 360 Controller",K::Xbox)==M::Xbox360,"Xbox 360");
 check(detect_controller_art("Nintendo Switch Pro",K::Xbox)==M::SwitchPro,"Switch Pro");
 check(detect_controller_art("Joy-Con (L/R)",K::Nintendo)==M::JoyConPair,"Joy-Con pair");
 check(detect_controller_art("Steam Controller",K::Xbox)==M::SteamController,"Steam Controller");
 check(detect_controller_art("Steam Deck",K::Xbox)==M::SteamDeck,"Steam Deck");
 check(detect_controller_art("unknown",K::Nintendo)==M::SwitchPro&&detect_controller_art("unknown",K::Keyboard)==M::XboxSeries,"family and unknown fallback");
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={800,600};io.DeltaTime=1.f/60;io.Fonts->AddFontDefault();init_controller_art(io.Fonts);io.Fonts->SetTexID(1);
 check(io.Fonts->TexPixelsUseColors&&io.Fonts->TexPixelsRGBA32,"colored atlas initialized");
 for(int model=0;model<kControllerArtCount;++model){
  const auto m=M(model);const auto& layout=controller_art_layout(m);for(const auto p:layout.buttons)check(p.x>=0&&p.x<=1&&p.y>=0&&p.y<=1,"normalized hit region bounds");
  check(controller_art_aspect(m)>.8f&&controller_art_aspect(m)<3,"model aspect ratio");
  const auto* rect=io.Fonts->GetCustomRectByIndex(model);check(rect->IsPacked(),"all images packed");
  size_t opaque=0,colored=0;for(int y=rect->Y;y<rect->Y+rect->Height;++y)for(int x=rect->X;x<rect->X+rect->Width;++x){auto px=io.Fonts->TexPixelsRGBA32[y*io.Fonts->TexWidth+x];if(px>>24)++opaque;if((px&255)!=((px>>8)&255))++colored;}
  check(opaque>1000,"decoded image contains opaque pixels");if(m!=M::JoyConPair)check(colored>100,"color pixels preserved");
 }
 Settings s;active=&s;
 auto frame=[&](int width,int height,bool appearing=false,bool open=true){
  io.DisplaySize={float(width),float(height)};ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("Controls",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoSavedSettings);controller_remapping(s,appearing,open);ImGui::End();ImGui::Render();
 };
 frame(800,600,true);auto original=s.controller_bindings;
 auto& state=controller_remap_state();state.capture=int(CtlB);state.action=int(CtlB);held[PhyEast]=true;frame(800,600);
 check(s.controller_bindings[CtlB]==PhyEast&&(s.controller_custom_mask&(1u<<CtlB))&&state.capture==-1,"fresh physical press remaps chosen action");
 check(s.controller_bindings[CtlY]==original[CtlY],"unrelated action preserved");
 check(controller_binding(s,CtlA)==PhySouth,"occupied button swaps use/confirm to previous jump button");
 {std::array<bool,16> used{};for(size_t a=0;a<16;++a){const int b=controller_binding(s,a);check(!used[b],"normal rebinding preserves one physical button per action");used[b]=true;}}
 state.capture=int(CtlY);frame(800,600);check(state.capture==int(CtlY),"held activation button cannot bind another action");
 held={};frame(800,600);held[PhyShoulderL]=true;frame(800,600);check(s.controller_bindings[CtlY]==PhyShoulderL,"released then pressed input binds");
 state.capture=1;cancel_controller_remapping();check(state.capture==-1,"page cancellation stops listening");
 state.capture=1;frame(800,600,true);check(state.capture==-1,"reopening menu cancels old capture");
 state.capture=int(CtlA);held={};held[PhyNorth]=true;const auto before_close=s.controller_bindings;frame(800,600,false,false);check(state.capture==-1&&s.controller_bindings==before_close,"closing/fading menu cannot capture gameplay input");
 s=Settings{};held={};state={};
 const auto mappings=s.controller_bindings;const auto custom=s.controller_custom_mask;
 if(argc>1){std::filesystem::create_directories(argv[1]);unsigned char* pixels=nullptr;int w=0,h=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);FILE* f=std::fopen((std::string(argv[1])+"/atlas.rgba").c_str(),"wb");std::fwrite(&w,4,1,f);std::fwrite(&h,4,1,f);std::fwrite(pixels,4,w*h,f);std::fclose(f);}
 for(int model=0;model<kControllerArtCount;++model){
  s=Settings{};s.controller_artwork=model+1;state={};frame(800,600);
  const auto* rect=io.Fonts->GetCustomRectByIndex(model);const ImVec2 uv{float(rect->X+1)/io.Fonts->TexWidth,float(rect->Y+1)/io.Fonts->TexHeight};
  ImVec2 p{},q{};bool found=false;
  for(const auto* list:ImGui::GetDrawData()->CmdLists)for(int v=0;v+2<list->VtxBuffer.Size;++v)if(list->VtxBuffer[v].uv.x==uv.x&&list->VtxBuffer[v].uv.y==uv.y){p=list->VtxBuffer[v].pos;q=list->VtxBuffer[v+2].pos;found=true;}
  check(found,"model artwork submitted to draw list");const auto east=controller_art_layout(M(model)).buttons[PhyEast];
  io.AddMousePosEvent(p.x+(q.x-p.x)*east.x,p.y+(q.y-p.y)*east.y);io.AddMouseButtonEvent(0,true);frame(800,600);io.AddMouseButtonEvent(0,false);frame(800,600);
  check(s.controller_bindings[CtlB]==PhyEast&&(s.controller_custom_mask&(1u<<CtlB)),"clicking model's east button remaps selected jump action");
  s=Settings{};s.controller_artwork=model+1;state={};io.AddMousePosEvent(-100,-100);frame(800,600);
  if(argc>1)dump(std::string(argv[1])+"/model-"+std::to_string(model),800,600);
  frame(640,360);if(argc>1)dump(std::string(argv[1])+"/small-"+std::to_string(model),640,360);
 }
 check(s.controller_bindings==mappings&&s.controller_custom_mask==custom,"artwork changes do not alter mappings");
 const auto path=std::filesystem::temp_directory_path()/("wonder-art-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".ini");
 s.controller_artwork=9;s.controller_bindings[CtlB]=PhyEast;s.controller_custom_mask|=1u<<CtlB;save_settings(path.string(),s);Settings restored;check(load_settings(path.string(),restored)&&restored.controller_artwork==9&&restored.controller_bindings==s.controller_bindings,"manual artwork and remappings persist");
 {std::ofstream(path)<<"controller_artwork=999\n";}check(load_settings(path.string(),restored)&&restored.controller_artwork==9,"artwork value clamped");std::filesystem::remove(path);
 Settings explicit_default;explicit_default.controller_bindings[CtlA]=default_controller_binding(CtlA,explicit_default.controller_preset);explicit_default.controller_custom_mask|=1u<<CtlA;
 check(assign_controller_button(explicit_default,CtlA,explicit_default.controller_bindings[CtlA])&&!(explicit_default.controller_custom_mask&(1u<<CtlA)),"returning to preset clears and persists redundant override");
 Settings duplicates;duplicates.controller_custom_mask=(1u<<CtlA)|(1u<<CtlX);duplicates.controller_bindings[CtlA]=PhySouth;duplicates.controller_bindings[CtlX]=PhySouth;
 check(assign_controller_button(duplicates,CtlY,PhySouth),"legacy duplicate layout can be rebound");
 for(size_t a=0;a<16;++a)if(a!=CtlY)check(controller_binding(duplicates,a)!=PhySouth,"requested button does not fire multiple legacy actions");
 check(!assign_controller_button(duplicates,16,0)&&!assign_controller_button(duplicates,0,16),"invalid assignments are rejected");
 ImGui::DestroyContext();std::puts("controller art: model detection, nine PNG atlas uploads, normalized regions, nine mouse assignments, conflict swapping, capture edges, cancellation, persistence and 18 layouts passed");
}
