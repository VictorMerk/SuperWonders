// The overlay's look (see overlay_ui.h). Style sizes follow Savage Wind's menu (runtime/ui/menu.mm style_init, MIT);
// the colours are SuperWonders' own and the font is Nunito (SIL OFL 1.1, third_party/fonts/Nunito).
#include "overlay/overlay_ui.h"
#include "overlay/layout_geometry.h"
#include "overlay/overlay.h"
#include "overlay/usability.h"
#include "button_font.h"
#include "overlay/controller_art.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>
#include <iterator>
#include <utility>

#include "imgui_internal.h"  // ImGuiStorage of the current window

#include <nx/runtime.h>
#include "platform.h"

namespace nx::overlay::ui {

namespace {

// Nunito SemiBold and ExtraBold, in the program (no files to ship or find). #embed: C23, a Clang and GCC 15
// extension in C++.
#if defined(__has_embed)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
#if __has_embed("../../../third_party/fonts/Nunito/Nunito-SemiBold.ttf")
#define NX_NUNITO 1
const unsigned char kBodyFont[] = {
#embed "../../../third_party/fonts/Nunito/Nunito-SemiBold.ttf"
};
const unsigned char kTitleFont[] = {
#embed "../../../third_party/fonts/Nunito/Nunito-ExtraBold.ttf"
};
#endif
#if __has_embed("../../../third_party/fonts/NotoFallback/NotoSansJP-menu.ttf")
#define NX_NOTO_MENU_JP 1
const unsigned char kNotoMenuJP[] = {
#embed "../../../third_party/fonts/NotoFallback/NotoSansJP-menu.ttf"
};
#endif
#if __has_embed("../../../third_party/fonts/NotoFallback/NotoSansKR-menu.ttf")
#define NX_NOTO_MENU_KR 1
const unsigned char kNotoMenuKR[] = {
#embed "../../../third_party/fonts/NotoFallback/NotoSansKR-menu.ttf"
};
#endif
#if __has_embed("../../../third_party/fonts/NotoFallback/NotoSansSC-menu.ttf")
#define NX_NOTO_MENU_SC 1
const unsigned char kNotoMenuSC[] = {
#embed "../../../third_party/fonts/NotoFallback/NotoSansSC-menu.ttf"
};
#endif
#if __has_embed("../../../third_party/fonts/NotoFallback/NotoSans-menu.ttf")
#define NX_NOTO_MENU_SANS 1
const unsigned char kNotoMenuSans[] = {
#embed "../../../third_party/fonts/NotoFallback/NotoSans-menu.ttf"
};
#endif
#if __has_embed("../../../third_party/fonts/NotoFallback/NotoSansArabic-menu.ttf")
#define NX_NOTO_MENU_ARABIC 1
const unsigned char kNotoMenuArabic[] = {
#embed "../../../third_party/fonts/NotoFallback/NotoSansArabic-menu.ttf"
};
#endif
#pragma clang diagnostic pop
#endif

// Base-font fallbacks (the in-game menu also merges script glyphs from OS fonts below).
const char* const kSystemFonts[] = {
#if defined(_WIN32)
    "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
#elif defined(__APPLE__)
    "/System/Library/Fonts/Supplemental/Arial.ttf",
#else
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansArabic-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/noto/NotoSansCJKkr-Regular.otf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
#endif
};

// Baked large and drawn smaller through FontGlobalScale (sharp up to 4K).
constexpr float kBodyBake = 40.0f, kTitleBake = 72.0f;
constexpr float kBody1080 = 29.0f;  // the menu rows at 1080 p (Savage Wind's size)

ImFont* g_body = nullptr;
ImFont* g_title = nullptr;
ImFont* g_wordmark = nullptr;
// The cover is much larger than menu headings. Bake only its six letters at
// display size, avoiding a large duplicate atlas of all localized menu glyphs.
constexpr float kWordmarkBake = 192.f;
const ImWchar kWordmarkRanges[] = {0x44,0x45,0x4e,0x4f,0x52,0x52,0x57,0x57,0};

// Latin-1 plus menu punctuation. Script glyphs used by the in-game menu and launcher are merged below.
const ImWchar kRanges[] = {0x0020, 0x00FF, 0x2013, 0x2014, 0x2018, 0x201E, 0x2022, 0x2026, 0x2190, 0x2193, 0};
const char kLocalizedGlyphs[] = "ÉÖÜßàáäçèéêíñóöùúüğışАБВГЗИКМОПРСУЭЯабвгдежзийклмнопрстуфхцчшыьэюяءأإئابةتجحخدذرزسشصضطظعغفقكلمنهوىيًُ٪—’、。いがきさしじすたでにのはまるれをァアィイウエォオキクグケゲコサシジスズタチッテトドナニネバパフブプボマミムメモャュラリルレロンー上下了位低作値像全内再出前力動右同声変定左常度後必性手振操放方日映時更期本用画異的示終置表要解言設語起速適部配量閉間限面音간거게국그기내너네뉴는능니다닫도동됩드등디또래량러레려력로롤료른를리링마메면명모방배버보본부브비빠상선설성세소속스시십싱아안앨어언업에오요용원위을음이일임자작재저적전정제조종준진쪽창처체출치컨케크키터텍템트튼티표프플필하한항해화후PlayGme fisStngModUpQurWAbYExFRCh.cOBkVHDvwT'Izq-KZLJjÀ파데폴더택현서사복열새고침완코빌야합되지않뒤페먼홈라법취新概ダ選択こ場所使うピセデ開く準備完ビてだ未戻ペ始先ョホそ他法ثФНДÇİ";

const char kAdditionalLocalizedGlyphs[] = "简体繁體快速设置视频音频控制性能叠加层窗口输出大小内部解析度全屏窗口置顶游戏音量静音游戏语言重新启动游戏以应用语言按键布局下方右侧帧率上限系统内存清理阈值低显存配置纹理显示画面帧同步升频锐化抗锯齿各向异性过滤垂直同步震动退出菜单语言帧时间图表位置键盘控制器原始动态屏幕原生快速設定影片音訊效能覆蓋層視窗輸出大小內部解析度全螢幕遊戲音量靜音遊戲語言重新啟動遊戲以套用語言按鍵配置影格率上限系統記憶體清理閾值低顯示記憶體設定材質顯示畫面升頻反鋸齒過濾垂直同步震動結束選單語言影格時間圖表鍵盤控制器原始動態螢幕原生";
const char kLauncherLocalizedGlyphs[] = "开始文件模组更新退出";
// Korean characters used in the help descriptions as well as the menu labels.
const char kKoreanHelpGlyphs[] = "가값강계과근꾸끄끕널넘높누느닙단당대된두듬딩록릅린묘미바번범분수습식압약없와유으은의인입있장절접줄중증직질집찢초최축충패할행확환황흐히";
const char kAdditionalHelpGlyphs[] = "ТЧщёآあえかずせちとどなほぼみめもらりわんギザ不与丢两中为也乱会保個側儲兩关减分切利効卡即压取变可号合后和回围圍圧増壓多够夠失対将將少幀平式強强微応慢或押拡择持换接提換撕擇改数整數斜明是晰替最會板柄染案檔檻次正此毎每毫況消済減渲滑為無状现現电省码确確碼秒稍算節範糊約素細組緣縮繪细缘缩耗脑腦至與节范荷號行裂見認調請變认请调負超足軽較輪较辨边近这进选這通進遅遺邊郭释釋銳門關闭離電需頂高鮮";

// Additional exact glyphs used by the localized launcher Controls page.
// Bundled subsets and installed script fonts share these bounded ranges.
const char kControlsLocalizedGlyphs[] = "\u041b\u0651\u3005\u3054\u3076\u3078\u3084\u30ab\u30e4\u30fb\u4e00\u4e26\u4e3b\u4e8e\u4e9b\u4eba\u4ece\u4fee\u5165\u5176\u521b\u5225\u5272\u5340\u5341\u5354\u53f0\u540d\u5728\u5730\u57df\u5907\u5916\u5b57\u5b9e\u5b9f\u5bb6\u5be6\u5bfc\u5c0e\u5c31\u5df2\u5e76\u5efa\u5f53\u5f9e\u623f\u62d4\u6307\u6377\u65ad\u65bc\u67a0\u680f\u6a5f\u6b04\u72ec\u7368\u73a9\u7559\u79fb\u7acb\u7d66\u7d9a\u7db2\u7dd2\u7ed9\u7eea\u7f51\u822a\u88dd\u89bd\u8def\u8fd4\u8fde\u9023\u9664\u9801\u9875\u9a13\u9a57\u9a8c\uac01\uacb0\uacbd\uaed8\ub098\ub3cc\ub428\ub7f0\ub86f\ubc0f\ubcc0\uc0c9\uc2ac\uc2e4\uc5f0\uceec\ud0d0\ud568\ud5a5\ud5d8\ud611\uff0c\u301c\uff1a";

const char* const kScriptFonts[] = {
#if defined(_WIN32)
    "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/meiryo.ttc",
    "C:/Windows/Fonts/msgothic.ttc", "C:/Windows/Fonts/YuGothR.ttc", "C:/Windows/Fonts/malgun.ttf",
    "C:/Windows/Fonts/gulim.ttc", "C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simsun.ttc",
    "C:/Windows/Fonts/mingliu.ttc",
#elif defined(__APPLE__)
    "/System/Library/Fonts/Supplemental/Arial Unicode.ttf", "/System/Library/Fonts/PingFang.ttc",
    "/System/Library/Fonts/AppleSDGothicNeo.ttc",
#else
    "/usr/share/fonts/truetype/noto/NotoSansArabic-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/noto/NotoSansCJKkr-Regular.otf",
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
#endif
};

struct Notice {
    std::string text;
    uint64_t at_ns;
};
std::mutex g_notice_lock;
std::deque<Notice> g_notices;
constexpr double kNoticeSeconds = 3.2;

}  // namespace

ImU32 fade(ImU32 c, float a) { return ImGui::GetColorU32(c, std::clamp(a, 0.0f, 1.0f)); }

ImU32 mix(ImU32 a, ImU32 b, float t) {
    const ImVec4 A = ImGui::ColorConvertU32ToFloat4(a), B = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(
        {A.x + (B.x - A.x) * t, A.y + (B.y - A.y) * t, A.z + (B.z - A.z) * t, A.w + (B.w - A.w) * t});
}

namespace {

// Quiet dark surfaces keep attention on game identity and the active control.
const Theme kTheme = {
    "Dark Games Hub", IM_COL32(129, 187, 255, 255), IM_COL32(194, 204, 220, 255), IM_COL32(241, 243, 247, 255),
    IM_COL32(179, 187, 201, 255), IM_COL32(130, 140, 157, 255), IM_COL32(23, 26, 32, 255), IM_COL32(31, 35, 43, 255),
    IM_COL32(13, 15, 19, 255), IM_COL32(20, 23, 29, 255), IM_COL32(134, 210, 170, 255), IM_COL32(237, 192, 119, 255),
    IM_COL32(242, 133, 141, 255)};

}  // namespace

const Theme& theme() { return kTheme; }

namespace {
ImVec4 v4(ImU32 c, float a = -1) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    if (a >= 0) v.w = a;
    return v;
}
}  // namespace

void apply_style(float height) {
    static float made_for = -1;
    const float k = std::clamp(height / 1080.0f, 0.5f, 3.0f);
    if (std::fabs(made_for - k) < 0.005f) return;
    made_for = k;
    const Theme& t = theme();
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    // (sizes: Savage Wind's menu at 1080 p)
    s.WindowRounding = 8, s.FrameRounding = 4, s.GrabRounding = 4, s.TabRounding = 0, s.ChildRounding = 4;
    s.PopupRounding = 6, s.ScrollbarRounding = 4;
    s.WindowPadding = {28, 24}, s.FramePadding = {14, 8}, s.ItemSpacing = {14, 12}, s.ItemInnerSpacing = {10, 8};
    s.WindowBorderSize = 0, s.PopupBorderSize = 1, s.ChildBorderSize = 0, s.TabBorderSize = 0;
    s.ScrollbarSize = 14, s.TabBarBorderSize = 1;
    s.DisabledAlpha = 0.45f;
    ImVec4* c = s.Colors;
    const ImVec4 accent = v4(t.accent), lift = v4(mix(t.panel, t.text, 0.10f), 1), lift2 = v4(mix(t.panel, t.text, 0.16f), 1);
    c[ImGuiCol_WindowBg] = v4(t.panel);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = v4(t.card, 0.98f);
    c[ImGuiCol_Border] = v4(t.text, 0.14f);
    c[ImGuiCol_Text] = v4(t.text);
    c[ImGuiCol_TextDisabled] = v4(t.text_off);
    c[ImGuiCol_FrameBg] = v4(mix(t.panel, t.text, 0.06f), 1);
    c[ImGuiCol_FrameBgHovered] = c[ImGuiCol_ButtonHovered] = c[ImGuiCol_Header] = lift;
    c[ImGuiCol_FrameBgActive] = c[ImGuiCol_ButtonActive] = c[ImGuiCol_HeaderHovered] = c[ImGuiCol_HeaderActive] = lift2;
    c[ImGuiCol_Button] = v4(mix(t.panel, t.text, 0.08f), 1);
    c[ImGuiCol_Tab] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabHovered] = ImVec4(1, 1, 1, 0.08f);
    c[ImGuiCol_TabSelected] = ImVec4(1, 1, 1, 0.07f);
    c[ImGuiCol_TabSelectedOverline] = ImVec4(0, 0, 0, 0);  // (the menu draws its own sliding underline)
    c[ImGuiCol_TabDimmed] = c[ImGuiCol_Tab], c[ImGuiCol_TabDimmedSelected] = c[ImGuiCol_TabSelected];
    c[ImGuiCol_CheckMark] = c[ImGuiCol_SliderGrabActive] = c[ImGuiCol_NavCursor] = c[ImGuiCol_PlotLines] = accent;
    c[ImGuiCol_SliderGrab] = c[ImGuiCol_ScrollbarGrabActive] = v4(t.accent, 0.7f);
    c[ImGuiCol_Separator] = ImVec4(1, 1, 1, 0.10f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(1, 1, 1, 0.14f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1, 1, 1, 0.24f);
    c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = c[ImGuiCol_WindowBg];
    s.ScaleAllSizes(k);
}

void load_fonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    g_body = g_title = g_wordmark = nullptr;
    ImFontConfig cfg;
    cfg.OversampleH = 2;
#ifdef NX_NUNITO
    cfg.FontDataOwnedByAtlas = false;  // (static data)
    g_body = io.Fonts->AddFontFromMemoryTTF((void*)kBodyFont, int(sizeof kBodyFont), kBodyBake, &cfg, kRanges);
    g_title = io.Fonts->AddFontFromMemoryTTF((void*)kTitleFont, int(sizeof kTitleFont), kTitleBake, &cfg, kRanges);
    g_wordmark = io.Fonts->AddFontFromMemoryTTF((void*)kTitleFont, int(sizeof kTitleFont), kWordmarkBake, &cfg, kWordmarkRanges);
    if (g_body) nx::log("overlay: font Nunito (embedded)");
#endif
    cfg.FontDataOwnedByAtlas = true;
    for (const char* path : kSystemFonts) {
        if (g_body) break;
        if (FILE* f = std::fopen(path, "rb")) {
            std::fclose(f);
            g_body = io.Fonts->AddFontFromFileTTF(path, kBodyBake, &cfg, kRanges);
            if (g_body) {
                g_title = io.Fonts->AddFontFromFileTTF(path, kTitleBake, &cfg, kRanges);
                g_wordmark = io.Fonts->AddFontFromFileTTF(path, kWordmarkBake, &cfg, kWordmarkRanges);
                nx::log("overlay: font %s", path);
            }
        }
    }
    if (!g_body) {
        ImFontConfig c;
        c.SizePixels = kBodyBake;
        g_body = io.Fonts->AddFontDefault(&c);
    }
    if (!g_title) g_title = g_body;
    if (!g_wordmark) g_wordmark = g_title;
    ImFontGlyphRangesBuilder glyph_builder;
    glyph_builder.AddText(kLocalizedGlyphs);
    glyph_builder.AddText(kAdditionalLocalizedGlyphs);
    glyph_builder.AddText(kLauncherLocalizedGlyphs);
    glyph_builder.AddText(kKoreanHelpGlyphs);
    glyph_builder.AddText(kAdditionalHelpGlyphs);
    glyph_builder.AddText(kControlsLocalizedGlyphs);
    // ImGui keeps this pointer in the font config and consumes it later, when the
    // Vulkan backend builds the atlas on its first NewFrame. Keep it alive beyond
    // this function; a local vector here leaves every merged fallback with a
    // dangling glyph-range pointer.
    static ImVector<ImWchar> localized_ranges;
    localized_ranges.clear();
    glyph_builder.BuildRanges(&localized_ranges);
#if defined(NX_NOTO_MENU_JP) || defined(NX_NOTO_MENU_KR) || defined(NX_NOTO_MENU_SC) || defined(NX_NOTO_MENU_SANS) || defined(NX_NOTO_MENU_ARABIC)
    ImFontConfig bundled;
    bundled.MergeMode = true;
    bundled.FontDataOwnedByAtlas = false;  // embedded static data; ImGui copies it into its atlas
    bundled.OversampleH = 2;
#ifdef NX_NOTO_MENU_JP
    bundled.DstFont = g_body;
    io.Fonts->AddFontFromMemoryTTF((void*)kNotoMenuJP, int(sizeof kNotoMenuJP), kBodyBake, &bundled,
                                   localized_ranges.Data);
    bundled.DstFont = g_title;
    io.Fonts->AddFontFromMemoryTTF((void*)kNotoMenuJP, int(sizeof kNotoMenuJP), kTitleBake, &bundled,
                                   localized_ranges.Data);
    nx::log("overlay: embedded Noto Sans JP glyph fallback");
#endif
#ifdef NX_NOTO_MENU_KR
    bundled.DstFont = g_body;
    io.Fonts->AddFontFromMemoryTTF((void*)kNotoMenuKR, int(sizeof kNotoMenuKR), kBodyBake, &bundled,
                                   localized_ranges.Data);
    bundled.DstFont = g_title;
    io.Fonts->AddFontFromMemoryTTF((void*)kNotoMenuKR, int(sizeof kNotoMenuKR), kTitleBake, &bundled,
                                   localized_ranges.Data);
    nx::log("overlay: embedded Noto Sans KR glyph fallback");
#endif
#ifdef NX_NOTO_MENU_SC
    bundled.DstFont = g_body;
    io.Fonts->AddFontFromMemoryTTF((void*)kNotoMenuSC, int(sizeof kNotoMenuSC), kBodyBake, &bundled,
                                   localized_ranges.Data);
    bundled.DstFont = g_title;
    io.Fonts->AddFontFromMemoryTTF((void*)kNotoMenuSC, int(sizeof kNotoMenuSC), kTitleBake, &bundled,
                                   localized_ranges.Data);
    nx::log("overlay: embedded Noto Sans SC glyph fallback");
#endif
#ifdef NX_NOTO_MENU_SANS
    bundled.DstFont = g_body;
    io.Fonts->AddFontFromMemoryTTF((void*)kNotoMenuSans, int(sizeof kNotoMenuSans), kBodyBake, &bundled,
                                   localized_ranges.Data);
    bundled.DstFont = g_title;
    io.Fonts->AddFontFromMemoryTTF((void*)kNotoMenuSans, int(sizeof kNotoMenuSans), kTitleBake, &bundled,
                                   localized_ranges.Data);
    nx::log("overlay: embedded Noto Sans glyph fallback");
#endif
#ifdef NX_NOTO_MENU_ARABIC
    bundled.DstFont = g_body;
    io.Fonts->AddFontFromMemoryTTF((void*)kNotoMenuArabic, int(sizeof kNotoMenuArabic), kBodyBake, &bundled,
                                   localized_ranges.Data);
    bundled.DstFont = g_title;
    io.Fonts->AddFontFromMemoryTTF((void*)kNotoMenuArabic, int(sizeof kNotoMenuArabic), kTitleBake, &bundled,
                                   localized_ranges.Data);
    nx::log("overlay: embedded Noto Sans Arabic glyph fallback");
#endif
#endif
    for (const char* path : kScriptFonts) {
        if (FILE* f = std::fopen(path, "rb")) {
            std::fclose(f);
            ImFontConfig fallback;
            fallback.MergeMode = true;
            fallback.FontNo = 0;  // some CJK system fonts are collections
            fallback.OversampleH = 2;
            fallback.DstFont = g_body;
            io.Fonts->AddFontFromFileTTF(path, kBodyBake, &fallback, localized_ranges.Data);
            fallback.DstFont = g_title;
            io.Fonts->AddFontFromFileTTF(path, kTitleBake, &fallback, localized_ranges.Data);
        }
    }
    io.FontDefault = g_body;
    init_controller_art(io.Fonts);
}

ImFont* body() { return g_body; }
ImFont* title() { return g_title; }
ImFont* wonder_wordmark() { return g_wordmark; }

float font_scale(float height) { return kBody1080 * std::clamp(height / 1080.0f, 0.5f, 3.0f) / kBodyBake; }

ImVec2 action_button_size(const char* label,float width) {
    const auto& style=ImGui::GetStyle();
    const char* end=ImGui::FindRenderedTextEnd(label);
    const float available=std::max(1.f,ImGui::GetContentRegionAvail().x);
    const float w=layout::button_width(available,width,ImGui::CalcTextSize(label,end).x,style.FramePadding.x);
    const float pad_x=std::min(style.FramePadding.x,w*.18f);
    const float text_width=std::max(.01f,w-2*pad_x);
    const float text_height=ImGui::CalcTextSize(label,end,false,text_width).y;
    const float pad_y=std::max(6.f,style.FramePadding.y);
    return {w,std::max(std::max(40.f,ImGui::GetFontSize()*1.7f),text_height+2*pad_y)};
}
bool action_button(const char* label,float width) {
    ImGuiWindow* window=ImGui::GetCurrentWindow();
    if(window->SkipItems || ImGui::GetContentRegionAvail().x<=0)return false;
    const ImGuiStyle& style=ImGui::GetStyle();
    const ImGuiID id=window->GetID(label); // native ### stable-ID semantics
    const ImVec2 size=action_button_size(label,width);
    const ImRect bounds(window->DC.CursorPos,ImVec2(window->DC.CursorPos.x+size.x,window->DC.CursorPos.y+size.y));
    ImGui::ItemSize(size,style.FramePadding.y);
    if(!ImGui::ItemAdd(bounds,id))return false;
    bool hovered=false,held=false;
    const bool pressed=ImGui::ButtonBehavior(bounds,id,&hovered,&held,ImGuiButtonFlags_None);
    ImGui::RenderNavCursor(bounds,id);
    ImGui::RenderFrame(bounds.Min,bounds.Max,ImGui::GetColorU32(held&&hovered?ImGuiCol_ButtonActive:hovered?ImGuiCol_ButtonHovered:ImGuiCol_Button),true,style.FrameRounding);
    const float pad_x=std::min(style.FramePadding.x,size.x*.18f),text_width=std::max(.01f,size.x-2*pad_x);
    const char* end=ImGui::FindRenderedTextEnd(label);
    const ImVec2 text_size=ImGui::CalcTextSize(label,end,false,text_width);
    const ImVec2 text_pos(bounds.Min.x+(size.x-text_size.x)*.5f,bounds.Min.y+(size.y-text_size.y)*.5f);
    window->DrawList->PushClipRect(bounds.Min,bounds.Max,true);
    window->DrawList->AddText(ImGui::GetFont(),ImGui::GetFontSize(),text_pos,ImGui::GetColorU32(ImGuiCol_Text),label,end,text_width);
    window->DrawList->PopClipRect();
    return pressed;
}

bool navigation_button(const char* label,bool selected,float width) {
    if(ImGui::GetCurrentWindow()->SkipItems || ImGui::GetContentRegionAvail().x<=0)return false;
    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(1,1,1,.045f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(1,1,1,.075f));
    ImGui::PushStyleColor(ImGuiCol_Text,ImGui::ColorConvertU32ToFloat4(selected?theme().text:theme().text_dim));
    const bool pressed=action_button(label,width);
    if(selected) {
        const auto p=ImGui::GetItemRectMin(),q=ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine({p.x,q.y-1},{q.x,q.y-1},fade(theme().accent),2);
    }
    ImGui::PopStyleColor(4);
    return pressed;
}

float approach(float current, float target, float rate) {
    if(usability_preferences().reduced_motion) return target;
    const float dt = std::min(ImGui::GetIO().DeltaTime, 0.1f);
    const float t = 1.0f - std::exp(-rate * dt);
    const float next = current + (target - current) * t;
    return std::fabs(target - next) < 0.002f ? target : next;
}

float eased(ImGuiID id, float target, float rate) {
    ImGuiStorage* st = ImGui::GetStateStorage();
    float* v = st->GetFloatRef(id, target);
    *v = approach(*v, target, rate);
    return *v;
}

float smoothstep(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

// ---- notices ----

void notify(const char* text) {
    if (!text || !*text) return;
    std::lock_guard lk(g_notice_lock);
    for (Notice& n : g_notices)  // the same notice again: shown longer, not twice
        if (n.text == text) {
            n.at_ns = plat::monotonic_ns();
            return;
        }
    g_notices.push_back({text, plat::monotonic_ns()});
    while (g_notices.size() > 3) g_notices.pop_front();
}

bool notices_active() {
    std::lock_guard lk(g_notice_lock);
    const uint64_t now = plat::monotonic_ns();
    while (!g_notices.empty() && double(now - g_notices.front().at_ns) / 1e9 > kNoticeSeconds) g_notices.pop_front();
    return !g_notices.empty();
}

// Savage Wind's "Picture saved" notice (menu.mm photo_overlay_draw): a dark card in the bottom-right corner that fades
// out over its last half second; here it also slides in from the right.
void draw_notices(float height) {
    std::deque<Notice> list;
    {
        std::lock_guard lk(g_notice_lock);
        list = g_notices;
    }
    if (list.empty()) return;
    const float k = std::clamp(height / 1080.0f, 0.5f, 3.0f);
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* f = body();
    const float fs = ImGui::GetFontSize() * 0.9f;
    const float margin=std::min(28*k,std::max(0.f,ds.x)*.04f),pad=std::min(16*k,std::max(0.f,ds.x)*.08f);
    const float text_width=std::max(1.f,std::min(640*k,ds.x-2*margin-2*pad-10*k));
    float y = ds.y - 28 * k;
    const uint64_t now = plat::monotonic_ns();
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        const float age = float(double(now - it->at_ns) / 1e9);
        const float in = usability_preferences().reduced_motion?1.f:smoothstep(age / 0.22f);
        const float out = age > kNoticeSeconds - 0.5f ? 1.0f - smoothstep((age - (kNoticeSeconds - 0.5f)) / 0.5f) : 1.0f;
        const float a = std::min(in, out);
        const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, text_width, it->text.c_str());
        const float w = std::min(std::max(1.f,ds.x-2*margin),ts.x+2*pad+10*k),h=ts.y+2*pad*.8f;
        const float x = ds.x - margin - w + (1.0f - in) * 40 * k;
        const ImVec2 p0(x, y - h), p1(x + w, y);
        dl->AddRectFilled(p0, p1, fade(theme().card, a * 0.95f), 10 * k);
        dl->AddRectFilled(p0, ImVec2(p0.x + 5 * k, p1.y), fade(theme().accent, a), 10 * k, ImDrawFlags_RoundCornersLeft);
        dl->PushClipRect(p0,p1,true);
        dl->AddText(f, fs, ImVec2(p0.x + pad + 5 * k, p0.y + pad * 0.8f), fade(theme().text, a), it->text.c_str(), nullptr,
                    text_width);
        dl->PopClipRect();
        y -= h + 10 * k;
    }
}

// ---- controller hints ----

void progress_bar(ImDrawList* dl, ImVec2 a, ImVec2 b, float frac, float alpha) {
    const Theme& th = theme();
    const float h = b.y - a.y, r = h * 0.5f;
    dl->AddRectFilled(a, b, fade(IM_COL32(255, 255, 255, 25), alpha), r);
    float fx = a.x + (b.x - a.x) * std::clamp(frac, 0.0f, 1.0f);
    if (fx <= a.x + 1) return;
    fx = std::max(fx, a.x + h);  // (at least its two round ends)
    dl->AddRectFilled(a, {fx, b.y}, fade(th.accent, alpha), r);
    // The sheen: a band a quarter of the filled part wide (at least 4 bar heights), the bar's full height. Its
    // brightness is worked out per slice, so it is soft in the middle, clear at its edges, and fades out over the round
    // ends instead of showing a square edge there.
    const float lo = a.x + r * 0.5f, hi = fx - r * 0.5f;
    if (hi - lo < 4) return;
    const float w = std::max(4 * h, (fx - a.x) * 0.25f);
    if(usability_preferences().reduced_motion)return;
    const float t = float(ImGui::GetTime());
    const float centre = lo - w * 0.5f + std::fmod(t / 1.6f, 1.0f) * (hi - lo + w);
    auto light = [&](float x) {
        const float band = std::max(0.0f, 1.0f - std::fabs(x - centre) / (w * 0.5f));
        const float ends = std::clamp(std::min(x - lo, hi - x) / r, 0.0f, 1.0f);
        return smoothstep(band) * smoothstep(ends);
    };
    const float x0 = std::max(lo, centre - w * 0.5f), x1 = std::min(hi, centre + w * 0.5f);
    if (x1 <= x0) return;
    constexpr int kSlices = 16;
    const ImU32 lit = IM_COL32(255, 248, 220, 120);
    for (int i = 0; i < kSlices; ++i) {
        const float sa = x0 + (x1 - x0) * float(i) / kSlices, sb = x0 + (x1 - x0) * float(i + 1) / kSlices;
        const ImU32 ca = fade(lit, alpha * light(sa)), cb = fade(lit, alpha * light(sb));
        // (full height inside the straight part; over the round ends the fade has it near clear already)
        const float inset_a = std::max(0.0f, r - std::min(sa - a.x, fx - sa)) * 0.9f;
        const float inset_b = std::max(0.0f, r - std::min(sb - a.x, fx - sb)) * 0.9f;
        const float inset = std::max(inset_a, inset_b);
        dl->AddRectFilledMultiColor({sa, a.y + inset}, {sb, b.y - inset}, ca, cb, cb, ca);
    }
}

static bool playstation_shape(const char* label) {
    return !std::strcmp(label,"Cross") || !std::strcmp(label,"Circle") ||
           !std::strcmp(label,"Square") || !std::strcmp(label,"Triangle");
}

static float badge_width(float h, const char* label) {
    if (playstation_shape(label)) return h;
    ImFont* font=body()?body():ImGui::GetFont();
    return std::max(h,font->CalcTextSizeA(h*.62f,FLT_MAX,0,label).x+h*.6f);
}

float badge(ImDrawList* dl, ImVec2 pos, float h, const char* label, float alpha) {
    ImFont* f = body()?body():ImGui::GetFont();
    const float fs = h * 0.62f;
    const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, label);
    const float w = badge_width(h,label);
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), fade(theme().text, alpha), h * 0.5f);
    if (playstation_shape(label)) {
        const ImVec2 c(pos.x+w*.5f,pos.y+h*.5f);
        const float r=h*.22f, stroke=std::max(1.0f,h*.065f);
        const ImU32 ink=fade(theme().panel | IM_COL32_A_MASK,alpha);
        if (!std::strcmp(label,"Circle")) dl->AddCircle(c,r,ink,24,stroke);
        else if (!std::strcmp(label,"Square")) dl->AddRect(ImVec2(c.x-r,c.y-r),ImVec2(c.x+r,c.y+r),ink,0,0,stroke);
        else if (!std::strcmp(label,"Triangle"))
            dl->AddTriangle(ImVec2(c.x,c.y-r),ImVec2(c.x+r,c.y+r),ImVec2(c.x-r,c.y+r),ink,stroke);
        else {
            dl->AddLine(ImVec2(c.x-r,c.y-r),ImVec2(c.x+r,c.y+r),ink,stroke);
            dl->AddLine(ImVec2(c.x-r,c.y+r),ImVec2(c.x+r,c.y-r),ink,stroke);
        }
        return w;
    }
    dl->AddText(f, fs, ImVec2(pos.x + (w - ts.x) * 0.5f, pos.y + (h - ts.y) * 0.5f), fade(theme().panel | IM_COL32_A_MASK, alpha),
                label);
    return w;
}

namespace {
struct ResolvedHint {std::string badge,text;};
std::vector<ResolvedHint> resolve_hints(const Hint* input,int n) {
    std::vector<ResolvedHint> resolved;
    if(!input||n<=0)return resolved;
    resolved.reserve(size_t(n));
    const auto settings=overlay::current_settings();auto kind=plat::controller_kind(0);
    if(settings.button_icons>=1&&settings.button_icons<=4)kind=static_cast<plat::ControllerKind>(settings.button_icons-1);
    else if(kind==plat::ControllerKind::Keyboard)
        for(uint32_t player=1;player<plat::kPlayerCount;++player)
            if(const auto candidate=plat::controller_kind(player);candidate!=plat::ControllerKind::Keyboard) {kind=candidate;break;}
    for(int i=0;i<n;++i) {
        ResolvedHint item{input[i].badge?input[i].badge:"",input[i].text?input[i].text:""};
        if(item.badge=="Menu")
            item.badge=kind==plat::ControllerKind::Keyboard?"F1":std::string(physical_button_name(kind,controller_binding(settings,CtlMinus)))+" + "+physical_button_name(kind,controller_binding(settings,CtlPlus));
        else {
            int action=-1;
            if(item.badge=="A")action=CtlA;if(item.badge=="B")action=CtlB;
            if(item.badge=="L")action=CtlL;if(item.badge=="R")action=CtlR;if(item.badge=="+")action=CtlPlus;
            if(action>=0) {
                if(kind==plat::ControllerKind::Keyboard)item.badge=action==CtlPlus?"F5":action==CtlA?"Enter":action==CtlB?"Esc":action==CtlL?"PgUp":"PgDn";
                else item.badge=physical_button_name(kind,controller_binding(settings,size_t(action)));
            }
        }
        resolved.push_back(std::move(item));
    }
    return resolved;
}
struct HintChip {std::string label;float x=0,y=0,width=0,height=0;};
struct HintGroup {std::vector<HintChip> chips;std::string text;float width=0,height=0,text_x=0,text_y=0,wrap=0;};
struct HintBlock {std::vector<HintGroup> groups;layout::Flow flow;};
HintBlock hint_block(const std::vector<ResolvedHint>& resolved,float width,float h) {
    width=layout::finite_positive(width);h=layout::finite_positive(h);
    const float fs=h*.78f,gap=h*.35f;
    ImFont* font=body()?body():ImGui::GetFont();
    HintBlock block;std::vector<layout::FlowExtent> extents;
    for(size_t i=0;i<resolved.size();) {
        HintGroup group;
        // Empty text joins the next badge: [L] [R] Category is one group.
        do {
            const auto& item=resolved[i++];
            if(!item.badge.empty()) {
                const float natural=badge_width(h,item.badge.c_str()),w=std::min(width,natural);
                float height=h;
                if(w<natural&&!playstation_shape(item.badge.c_str())) {
                    const float pad=std::min(h*.25f,w*.15f);
                    height=std::max(h,font->CalcTextSizeA(h*.62f,FLT_MAX,std::max(.01f,w-2*pad),item.badge.c_str()).y+h*.4f);
                }
                group.chips.push_back({item.badge,0,0,w,height});
            }
            group.text=item.text;
        } while(group.text.empty()&&i<resolved.size());
        if(group.chips.empty()&&group.text.empty())continue;
        float prefix=0,badge_height=0;
        for(const auto& chip:group.chips) {prefix+=chip.width+gap;badge_height=std::max(badge_height,chip.height);}
        if(group.text.empty()&&!group.chips.empty())prefix-=gap;
        const float natural=font->CalcTextSizeA(fs,FLT_MAX,0,group.text.c_str()).x;
        const bool stacked=!group.chips.empty()&&prefix+std::min(natural,fs*5)>width;
        if(stacked) {
            std::vector<layout::FlowExtent> badges;
            for(const auto& chip:group.chips)badges.push_back({chip.width,chip.height});
            const auto positions=layout::flow(badges,width,gap,h*.25f);
            for(size_t j=0;j<group.chips.size();++j) {group.chips[j].x=positions.items[j].x;group.chips[j].y=positions.items[j].y;}
            group.width=width;group.height=positions.height;group.wrap=width;
            if(!group.text.empty()) {
                const ImVec2 text=font->CalcTextSizeA(fs,FLT_MAX,width,group.text.c_str());
                group.text_x=std::max(0.f,(width-text.x)*.5f);group.text_y=group.height+gap;group.height+=gap+text.y;
            }
        } else {
            group.wrap=std::max(.01f,width-prefix);
            const ImVec2 text=group.text.empty()?ImVec2():font->CalcTextSizeA(fs,FLT_MAX,group.wrap,group.text.c_str());
            group.width=std::min(width,prefix+text.x);group.height=std::max(badge_height,text.y);
            float x=0;
            for(auto& chip:group.chips) {chip.x=x;chip.y=(group.height-chip.height)*.5f;x+=chip.width+gap;}
            group.text_x=prefix;group.text_y=(group.height-text.y)*.5f;
        }
        extents.push_back({group.width,group.height});block.groups.push_back(std::move(group));
    }
    block.flow=layout::flow(extents,width,h*1.1f,h*.35f);return block;
}
void hint_chip(ImDrawList* dl,ImVec2 pos,float h,const HintChip& chip,float alpha) {
    const float natural=badge_width(h,chip.label.c_str());
    if(chip.width>=natural-.01f) {badge(dl,pos,h,chip.label.c_str(),alpha);return;}
    if(playstation_shape(chip.label.c_str())) {
        badge(dl,ImVec2(pos.x,pos.y+(chip.height-chip.width)*.5f),chip.width,chip.label.c_str(),alpha);return;
    }
    ImFont* font=body()?body():ImGui::GetFont();
    const float pad=std::min(h*.25f,chip.width*.15f),wrap=std::max(.01f,chip.width-2*pad);
    const ImVec2 text=font->CalcTextSizeA(h*.62f,FLT_MAX,wrap,chip.label.c_str());
    dl->AddRectFilled(pos,ImVec2(pos.x+chip.width,pos.y+chip.height),fade(theme().text,alpha),std::min(chip.width,chip.height)*.5f);
    dl->AddText(font,h*.62f,ImVec2(pos.x+pad,pos.y+(chip.height-text.y)*.5f),fade(theme().panel|IM_COL32_A_MASK,alpha),chip.label.c_str(),nullptr,wrap);
}
}
float hints_height(float max_width,float h,const Hint* input,int n) {
    if(!input||n<=0)return 0;
    if(max_width<=0)return std::max(0.f,h);
    return hint_block(resolve_hints(input,n),max_width,h).flow.height;
}
void hints(ImDrawList* dl,float centre_x,float y,float h,const Hint* input,int n,float alpha,float max_width) {
    if(!dl||!input||n<=0||h<=0)return;
    const auto list=resolve_hints(input,n);
    ImFont* font=body()?body():ImGui::GetFont();const float fs=h*.78f,gap=h*.35f,spacing=h*1.1f;
    if(max_width>0) {
        const auto block=hint_block(list,max_width,h);
        const float left=centre_x-max_width*.5f,top=y-block.flow.height*.5f;
        for(size_t i=0;i<block.groups.size();++i) {
            const auto& group=block.groups[i];const auto& at=block.flow.items[i];
            const ImVec2 pos(left+at.x,top+at.y),end(pos.x+group.width,pos.y+group.height);
            dl->PushClipRect(pos,end,true);
            for(const auto& chip:group.chips)hint_chip(dl,ImVec2(pos.x+chip.x,pos.y+chip.y),h,chip,alpha);
            if(!group.text.empty())dl->AddText(font,fs,ImVec2(pos.x+group.text_x,pos.y+group.text_y),fade(theme().text_dim,alpha),group.text.c_str(),nullptr,group.wrap);
            dl->PopClipRect();
        }
        return;
    }
    // Existing callers keep their single-line center until they opt into a
    // bounded footer and reserve hints_height(). No hidden font reduction.
    float total=0;
    for(size_t i=0;i<list.size();++i) {
        const auto& item=list[i];const float bw=item.badge.empty()?0:badge_width(h,item.badge.c_str())+gap;
        total+=bw+font->CalcTextSizeA(fs,FLT_MAX,0,item.text.c_str()).x+(i+1<list.size()&&!item.text.empty()?spacing:0);
    }
    float x=centre_x-total*.5f;
    for(const auto& item:list) {
        if(!item.badge.empty())x+=badge(dl,ImVec2(x,y-h*.5f),h,item.badge.c_str(),alpha)+gap;
        const ImVec2 text=font->CalcTextSizeA(fs,FLT_MAX,0,item.text.c_str());
        dl->AddText(font,fs,ImVec2(x,y-text.y*.5f),fade(theme().text_dim,alpha),item.text.c_str());
        x+=text.x+(!item.text.empty()?spacing:0);
    }
}

}  // namespace nx::overlay::ui
