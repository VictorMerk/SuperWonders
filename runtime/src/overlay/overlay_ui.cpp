// The overlay's look (see overlay_ui.h). Style sizes follow Savage Wind's menu (runtime/ui/menu.mm style_init, MIT);
// the colours are SuperWonders' own and the font is Nunito (SIL OFL 1.1, third_party/fonts/Nunito).
#include "overlay/overlay_ui.h"
#include "overlay/overlay.h"
#include "overlay/usability.h"
#include "button_font.h"
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

// Latin-1 plus menu punctuation. Script glyphs used by the in-game menu and launcher are merged below.
const ImWchar kRanges[] = {0x0020, 0x00FF, 0x2013, 0x2014, 0x2018, 0x201E, 0x2022, 0x2026, 0x2190, 0x2193, 0};
const char kLocalizedGlyphs[] = "ÉÖÜßàáäçèéêíñóöùúüğışАБВГЗИКМОПРСУЭЯабвгдежзийклмнопрстуфхцчшыьэюяءأإئابةتجحخدذرزسشصضطظعغفقكلمنهوىيًُ٪—’、。いがきさしじすたでにのはまるれをァアィイウエォオキクグケゲコサシジスズタチッテトドナニネバパフブプボマミムメモャュラリルレロンー上下了位低作値像全内再出前力動右同声変定左常度後必性手振操放方日映時更期本用画異的示終置表要解言設語起速適部配量閉間限面音간거게국그기내너네뉴는능니다닫도동됩드등디또래량러레려력로롤료른를리링마메면명모방배버보본부브비빠상선설성세소속스시십싱아안앨어언업에오요용원위을음이일임자작재저적전정제조종준진쪽창처체출치컨케크키터텍템트튼티표프플필하한항해화후PlayGme fisStngModUpQurWAbYExFRCh.cOBkVHDvwT'Izq-KZLJjÀ파데폴더택현서사복열새고침완코빌야합되지않뒤페먼홈라법취新概ダ選択こ場所使うピセデ開く準備完ビてだ未戻ペ始先ョホそ他法ثФНДÇİ";

const char kAdditionalLocalizedGlyphs[] = "简体繁體快速设置视频音频控制性能叠加层窗口输出大小内部解析度全屏窗口置顶游戏音量静音游戏语言重新启动游戏以应用语言按键布局下方右侧帧率上限系统内存清理阈值低显存配置纹理显示画面帧同步升频锐化抗锯齿各向异性过滤垂直同步震动退出菜单语言帧时间图表位置键盘控制器原始动态屏幕原生快速設定影片音訊效能覆蓋層視窗輸出大小內部解析度全螢幕遊戲音量靜音遊戲語言重新啟動遊戲以套用語言按鍵配置影格率上限系統記憶體清理閾值低顯示記憶體設定材質顯示畫面升頻反鋸齒過濾垂直同步震動結束選單語言影格時間圖表鍵盤控制器原始動態螢幕原生";
const char kLauncherLocalizedGlyphs[] = "开始文件模组更新退出";
// Korean characters used in the help descriptions as well as the menu labels.
const char kKoreanHelpGlyphs[] = "가값강계과근꾸끄끕널넘높누느닙단당대된두듬딩록릅린묘미바번범분수습식압약없와유으은의인입있장절접줄중증직질집찢초최축충패할행확환황흐히";
const char kAdditionalHelpGlyphs[] = "ТЧщёآあえかずせちとどなほぼみめもらりわんギザ不与丢两中为也乱会保個側儲兩关减分切利効卡即压取变可号合后和回围圍圧増壓多够夠失対将將少幀平式強强微応慢或押拡择持换接提換撕擇改数整數斜明是晰替最會板柄染案檔檻次正此毎每毫況消済減渲滑為無状现現电省码确確碼秒稍算節範糊約素細組緣縮繪细缘缩耗脑腦至與节范荷號行裂見認調請變认请调負超足軽較輪较辨边近这进选這通進遅遺邊郭释釋銳門關闭離電需頂高鮮";

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

// "Flower Garden": sunflower gold and mint over ink-green panels, with warm high-contrast text.
const Theme kTheme = {
    "Flower Garden", IM_COL32(255, 204, 72, 255), IM_COL32(105, 222, 182, 255), IM_COL32(251, 247, 230, 255),
    IM_COL32(200, 216, 210, 255), IM_COL32(130, 155, 150, 255), IM_COL32(16, 35, 38, 246), IM_COL32(23, 46, 48, 250),
    IM_COL32(9, 24, 29, 255), IM_COL32(23, 54, 53, 255), IM_COL32(110, 232, 140, 255), IM_COL32(255, 206, 72, 255),
    IM_COL32(255, 96, 112, 255)};

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
    s.WindowRounding = 14, s.FrameRounding = 8, s.GrabRounding = 8, s.TabRounding = 8, s.ChildRounding = 10;
    s.PopupRounding = 10, s.ScrollbarRounding = 8;
    s.WindowPadding = {28, 24}, s.FramePadding = {14, 8}, s.ItemSpacing = {14, 12}, s.ItemInnerSpacing = {10, 8};
    s.WindowBorderSize = 0, s.PopupBorderSize = 1, s.ChildBorderSize = 0, s.TabBorderSize = 0;
    s.ScrollbarSize = 14, s.TabBarBorderSize = 1;
    s.DisabledAlpha = 0.45f;
    ImVec4* c = s.Colors;
    const ImVec4 accent = v4(t.accent), lift = v4(mix(t.panel, t.accent2, 0.22f), 1), lift2 = v4(mix(t.panel, t.accent2, 0.34f), 1);
    c[ImGuiCol_WindowBg] = v4(t.panel);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = v4(t.card, 0.98f);
    c[ImGuiCol_Border] = v4(t.accent, 0.35f);
    c[ImGuiCol_Text] = v4(t.text);
    c[ImGuiCol_TextDisabled] = v4(t.text_off);
    c[ImGuiCol_FrameBg] = v4(mix(t.panel, t.accent2, 0.12f), 1);
    c[ImGuiCol_FrameBgHovered] = c[ImGuiCol_ButtonHovered] = c[ImGuiCol_Header] = lift;
    c[ImGuiCol_FrameBgActive] = c[ImGuiCol_ButtonActive] = c[ImGuiCol_HeaderHovered] = c[ImGuiCol_HeaderActive] = lift2;
    c[ImGuiCol_Button] = v4(mix(t.panel, t.accent2, 0.14f), 1);
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
    g_body = g_title = nullptr;
    ImFontConfig cfg;
    cfg.OversampleH = 2;
#ifdef NX_NUNITO
    cfg.FontDataOwnedByAtlas = false;  // (static data)
    g_body = io.Fonts->AddFontFromMemoryTTF((void*)kBodyFont, int(sizeof kBodyFont), kBodyBake, &cfg, kRanges);
    g_title = io.Fonts->AddFontFromMemoryTTF((void*)kTitleFont, int(sizeof kTitleFont), kTitleBake, &cfg, kRanges);
    if (g_body) nx::log("overlay: font Nunito (embedded)");
#endif
    cfg.FontDataOwnedByAtlas = true;
    for (const char* path : kSystemFonts) {
        if (g_body) break;
        if (FILE* f = std::fopen(path, "rb")) {
            std::fclose(f);
            g_body = io.Fonts->AddFontFromFileTTF(path, kBodyBake, &cfg, kRanges);
            if (g_body) g_title = io.Fonts->AddFontFromFileTTF(path, kTitleBake, &cfg, kRanges), nx::log("overlay: font %s", path);
        }
    }
    if (!g_body) {
        ImFontConfig c;
        c.SizePixels = kBodyBake;
        g_body = io.Fonts->AddFontDefault(&c);
    }
    if (!g_title) g_title = g_body;
    ImFontGlyphRangesBuilder glyph_builder;
    glyph_builder.AddText(kLocalizedGlyphs);
    glyph_builder.AddText(kAdditionalLocalizedGlyphs);
    glyph_builder.AddText(kLauncherLocalizedGlyphs);
    glyph_builder.AddText(kKoreanHelpGlyphs);
    glyph_builder.AddText(kAdditionalHelpGlyphs);
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
}

ImFont* body() { return g_body; }
ImFont* title() { return g_title; }

float font_scale(float height) { return kBody1080 * std::clamp(height / 1080.0f, 0.5f, 3.0f) / kBodyBake; }

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
    const float fs = ImGui::GetFontSize() * 0.9f, pad = 16 * k;
    float y = ds.y - 28 * k;
    const uint64_t now = plat::monotonic_ns();
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        const float age = float(double(now - it->at_ns) / 1e9);
        const float in = smoothstep(age / 0.22f);
        const float out = age > kNoticeSeconds - 0.5f ? 1.0f - smoothstep((age - (kNoticeSeconds - 0.5f)) / 0.5f) : 1.0f;
        const float a = std::min(in, out);
        const ImVec2 ts = f->CalcTextSizeA(fs, 640 * k, 640 * k, it->text.c_str());
        const float w = ts.x + 2 * pad + 10 * k, h = ts.y + 2 * pad * 0.8f;
        const float x = ds.x - 28 * k - w + (1.0f - in) * 40 * k;
        const ImVec2 p0(x, y - h), p1(x + w, y);
        dl->AddRectFilled(p0, p1, fade(theme().card, a * 0.95f), 10 * k);
        dl->AddRectFilled(p0, ImVec2(p0.x + 5 * k, p1.y), fade(theme().accent, a), 10 * k, ImDrawFlags_RoundCornersLeft);
        dl->AddText(f, fs, ImVec2(p0.x + pad + 5 * k, p0.y + pad * 0.8f), fade(theme().text, a), it->text.c_str(), nullptr,
                    640 * k);
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
    return std::max(h, body()->CalcTextSizeA(h * 0.62f, FLT_MAX, 0, label).x + h * 0.6f);
}

float badge(ImDrawList* dl, ImVec2 pos, float h, const char* label, float alpha) {
    ImFont* f = body();
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

void hints(ImDrawList* dl, float centre_x, float y, float h, const Hint* input, int n, float alpha) {
    std::vector<std::string> labels(size_t(n), std::string{});
    std::vector<Hint> resolved(input, input + n);
    const auto settings = overlay::current_settings();
    auto kind = plat::controller_kind(0);
    // The launcher also accepts a controller assigned after the keyboard slot.
    // Match the explicit in-game icon preference when one is selected.
    if (settings.button_icons >= 1 && settings.button_icons <= 4)
        kind = static_cast<plat::ControllerKind>(settings.button_icons - 1);
    else if (kind == plat::ControllerKind::Keyboard)
        for (uint32_t player = 1; player < plat::kPlayerCount; ++player)
            if (const auto candidate = plat::controller_kind(player); candidate != plat::ControllerKind::Keyboard) {
                kind = candidate;
                break;
            }
    for (int i = 0; i < n; ++i) {
        if (!input[i].badge) continue;
        const char* original = input[i].badge;
        if (!std::strcmp(original,"Menu")) {
            labels[i]=kind==plat::ControllerKind::Keyboard ? "F1" :
                std::string(physical_button_name(kind,controller_binding(settings,CtlMinus)))+" + "+
                physical_button_name(kind,controller_binding(settings,CtlPlus));
            resolved[i].badge=labels[i].c_str();
            continue;
        }
        int action = -1;
        if (!std::strcmp(original, "A")) action = CtlA;
        if (!std::strcmp(original, "B")) action = CtlB;
        if (!std::strcmp(original, "L")) action = CtlL;
        if (!std::strcmp(original, "R")) action = CtlR;
        if (!std::strcmp(original, "+")) action = CtlPlus;
        if (action < 0) continue;
        if (kind == plat::ControllerKind::Keyboard)
            labels[i] = action == CtlPlus ? "F5" : action == CtlA ? "Enter" : action == CtlB ? "Esc" : action == CtlL ? "PgUp" : "PgDn";
        else labels[i] = physical_button_name(kind, controller_binding(settings, size_t(action)));
        resolved[i].badge = labels[i].c_str();
    }
    const Hint* list = resolved.data();
    ImFont* f = body();
    const float fs = h * 0.78f, gap = h * 0.35f, spacing = h * 1.1f;
    float total = 0;
    for (int i = 0; i < n; ++i) {
        float bw = 0;
        if (list[i].badge) {
            bw = badge_width(h,list[i].badge) + gap;
        }
        total += bw + f->CalcTextSizeA(fs, FLT_MAX, 0, list[i].text).x + (i + 1 < n ? (*list[i].text ? spacing : 0) : 0);
    }
    float x = centre_x - total * 0.5f;
    for (int i = 0; i < n; ++i) {
        if (list[i].badge) x += badge(dl, ImVec2(x, y - h * 0.5f), h, list[i].badge, alpha) + gap;
        const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, list[i].text);
        dl->AddText(f, fs, ImVec2(x, y - ts.y * 0.5f), fade(theme().text_dim, alpha), list[i].text);
        x += ts.x + (*list[i].text ? spacing : 0);  // (an empty text: the next badge belongs with this one)
    }
}

}  // namespace nx::overlay::ui
