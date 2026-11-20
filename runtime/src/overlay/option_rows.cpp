// Two-pane settings pages (see option_rows.h). Ported from Savage Wind's runtime/ui/option_rows.mm (MIT, Copyright (c)
// 2026 Revivedx and the Savage Wind Recomp contributors): row(), the arrows, the description pane and the left/right
// key ownership follow it; hover colours soften while keyboard focus and help update immediately.
#include "overlay/option_rows.h"
#include "overlay/layout_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"  // SetKeyOwner, SetFocusID, NavCursorVisible

#include "overlay/overlay_ui.h"
#include "overlay/usability.h"

namespace nx::overlay::rows {

namespace {

float g_k = 1;  // style scale (1 at 1080 p)
std::string g_desc_hover, g_desc_focus, g_desc_shown, g_desc_first, g_note;
std::string g_desc_retained;
ImGuiID g_desc_id = 0, g_desc_hover_id = 0, g_desc_focus_id = 0, g_desc_first_id = 0;

bool g_next_disabled = false;
int g_indent = 0;
bool g_focus_req = false, g_take_first = false;
bool g_compact=false;
ui::layout::Panes g_panes;
ImVec2 g_page_origin;

bool pressed(ImGuiKey a, ImGuiKey b, ImGuiKey c) {
    return ImGui::IsKeyPressed(a) || ImGui::IsKeyPressed(b) || ImGui::IsKeyPressed(c);
}

// One row: draws it, handles A / click and left / right while selected. Returns 2 (A / click), -1 / +1 (left /
// right) or 0. arrows: ◀ value ▶ (a choice of several, or a stepper), the arrows click too.
int row(const char* label, const std::string& value, bool changed, const char* desc, bool up = true, bool arrows = false,
        bool highlight = false, bool primary = false) {
    ImGui::PushID(label);
    if(g_indent) {
        const float inset=std::min(g_indent*28*g_k,std::max(0.f,ImGui::GetContentRegionAvail().x-160*g_k));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX()+inset);
    }
    const ImVec2 p=ImGui::GetCursorScreenPos();
    const float w=std::max(1.f,ImGui::GetContentRegionAvail().x),fs=ImGui::GetFontSize();
    // Sentence case makes the list scannable; IDs still use the original label.
    std::string text(label);
    (void)up;
    if(const size_t cut=text.find("##");cut!=std::string::npos)text.resize(cut);
    const auto columns=ui::layout::row_columns(w,g_k,fs,ImGui::CalcTextSize(text.c_str()).x,
        ImGui::CalcTextSize(value.c_str()).x,!value.empty(),arrows);
    const ImVec2 label_size=ImGui::CalcTextSize(text.c_str(),nullptr,false,columns.label_width);
    const ImVec2 value_size=value.empty()?ImVec2():ImGui::CalcTextSize(value.c_str(),nullptr,false,columns.value_width);
    const auto vertical=ui::layout::row_vertical(columns,g_k,fs,label_size.y,value_size.y,!value.empty());
    const float h=std::max(vertical.height,primary?88*g_k:0.f);
    const float offset_y=(h-vertical.height)*.5f;
    ImGui::PushStyleColor(ImGuiCol_NavCursor,ImVec4(0,0,0,0));
    const bool clicked=ImGui::InvisibleButton("##row",ImVec2(w,h),ImGuiButtonFlags_EnableNav);
    ImGui::PopStyleColor();
    const ImGuiID id=ImGui::GetItemID();
    if(g_take_first) {
        g_take_first=false;ImGui::SetFocusID(id,ImGui::GetCurrentWindow());
        ImGui::SetNavCursorVisible(true);ImGui::SetScrollHereY(0);
    }
    const bool off=g_next_disabled;g_next_disabled=false;
    const bool hov=ImGui::IsItemHovered();
    const bool foc=ImGui::IsItemFocused()&&ImGui::GetCurrentContext()->NavCursorVisible;
    int step=clicked&&!off?2:0;
    if(foc&&!off) {
        if(pressed(ImGuiKey_GamepadDpadLeft,ImGuiKey_GamepadLStickLeft,ImGuiKey_LeftArrow))step=-1;
        if(pressed(ImGuiKey_GamepadDpadRight,ImGuiKey_GamepadLStickRight,ImGuiKey_RightArrow))step=1;
        for(ImGuiKey key:{ImGuiKey_GamepadDpadLeft,ImGuiKey_GamepadDpadRight,ImGuiKey_GamepadLStickLeft,
                         ImGuiKey_GamepadLStickRight,ImGuiKey_LeftArrow,ImGuiKey_RightArrow})ImGui::SetKeyOwner(key,id);
    }
    if(foc)g_desc_focus=desc?desc:"",g_desc_focus_id=id;
    if(hov)g_desc_hover=desc?desc:"",g_desc_hover_id=id;
    if(g_desc_first.empty()&&desc)g_desc_first=desc,g_desc_first_id=id;
    if(id==g_desc_id&&desc)g_desc_retained=desc;

    // Mouse hover may soften; keyboard/controller focus responds immediately.
    const float hover=ui::eased(id+1,hov?1.f:0.f,22.f),t=foc?1.f:hover;
    ImDrawList* dl=ImGui::GetWindowDrawList();
    const float radius=4*g_k;const ImVec2 q(p.x+w,p.y+h);
    if(primary)dl->AddRectFilled(p,q,ui::fade(off?ui::theme().card:ui::mix(ui::theme().accent,ui::theme().text,t*.15f)),radius);
    else {
        if(t>.01f)dl->AddRectFilled(p,q,ui::fade(ui::theme().text,t*.065f),radius);
        dl->AddLine({p.x+columns.padding,q.y},{q.x-columns.padding,q.y},ui::fade(ui::theme().text,.10f),1);
    }
    if(foc)dl->AddRect(p,q,ui::fade(ui::theme().accent),radius,0,2*std::max(1.f,g_k));
    if(highlight) {
        dl->AddRect(p,q,ui::fade(ui::theme().accent,.8f),radius,0,2*std::max(1.f,g_k));
    }
    dl->PushClipRect(p,q,true);
    const ImU32 label_ink=off?ui::theme().text_off:primary?ui::theme().back_top:ui::theme().text;
    dl->AddText(ImGui::GetFont(),fs,ImVec2(p.x+columns.padding,p.y+vertical.label_y+offset_y),
        ui::fade(label_ink),text.c_str(),nullptr,columns.label_width);
    if(!value.empty()) {
        const float control_x=p.x+columns.control_x,control_y=p.y+vertical.control_y+offset_y;
        float value_x=control_x;
        if(arrows) {
            const float target=columns.arrow_width,cy=control_y+vertical.control_height*.5f;
            const float glyph=std::min(fs*.32f,target*.25f);
            const float lx=control_x+target*.5f,rx=control_x+columns.control_width-target*.5f;
            const ImU32 ink=ui::fade(off?ui::theme().text_off:ui::mix(ui::theme().text_dim,ui::theme().accent,t));
            dl->AddTriangleFilled(ImVec2(lx-glyph,cy),ImVec2(lx+glyph,cy-glyph*.8f),ImVec2(lx+glyph,cy+glyph*.8f),ink);
            dl->AddTriangleFilled(ImVec2(rx+glyph,cy),ImVec2(rx-glyph,cy-glyph*.8f),ImVec2(rx-glyph,cy+glyph*.8f),ink);
            value_x+=target+columns.gap+std::max(0.f,(columns.value_width-value_size.x)*.5f);
            // Nav activation has no mouse hit coordinate. In stacked rows only
            // actual arrow cells, not the label above them, can step backwards.
            if(clicked&&!off&&ImGui::GetCurrentContext()->NavActivateId!=id&&ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                const ImVec2 mouse=ImGui::GetIO().MousePos;
                if(mouse.y>=control_y&&mouse.y<=control_y+vertical.control_height) {
                    if(mouse.x>=control_x&&mouse.x<=control_x+target)step=-1;
                    else if(mouse.x>=control_x+columns.control_width-target&&mouse.x<=control_x+columns.control_width)step=1;
                }
            }
        }
        const float value_y=control_y+(vertical.control_height-value_size.y)*.5f;
        dl->AddText(ImGui::GetFont(),fs,ImVec2(value_x,value_y),
            ui::fade(off?ui::theme().text_off:primary?ui::theme().back_top:changed?ui::theme().text:ui::theme().text_dim),value.c_str(),nullptr,columns.value_width);
    }
    dl->PopClipRect();ImGui::PopID();return step;
}

}  // namespace

void focus_first() { g_focus_req = true; }
bool consume_focus_request() { const bool requested=g_focus_req;g_focus_req=false;return requested; }
void disable_next() { g_next_disabled = true; }
void indent(int step) { g_indent = std::max(0, g_indent + step); }
void page_note(const char* text) { g_note = text ? text : ""; }

void begin(const char* id, float height) {
    g_k = std::clamp(height / 1080.0f, 0.5f, 3.0f);
    static std::string page;  // (another page: its first row's description, not the last page's)
    if (page != id) page = id, g_desc_shown.clear();
    g_take_first = g_focus_req;
    g_focus_req = false;
    if (g_take_first) ImGui::NavMoveRequestCancel();
    g_indent = 0;g_next_disabled=false;
    g_desc_hover.clear(), g_desc_focus.clear(), g_desc_first.clear(), g_note.clear();
    g_desc_retained.clear();
    g_desc_hover_id = g_desc_focus_id = g_desc_first_id = 0;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::PushID(id);
    g_page_origin=ImGui::GetCursorScreenPos();
    g_panes=ui::layout::panes(avail.x,avail.y,g_k,ImGui::GetFontSize());g_compact=g_panes.compact;
    ImGui::BeginChild("##rows",ImVec2(g_panes.rows_width,g_panes.rows_height),ImGuiChildFlags_NavFlattened);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 5 * g_k));  // (rows close)
}

void section(const char* title) {
    const std::string t(title);
    if (ImGui::GetCursorPosY() > ImGui::GetStyle().WindowPadding.y + 1) ImGui::Dummy(ImVec2(0, 12 * g_k));
    ImGui::SetWindowFontScale(0.82f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::theme().accent2));
    ImGui::PushTextWrapPos(0);ImGui::TextUnformatted(t.c_str());ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);
}

bool toggle(const char* label, bool* v, bool def, const char* desc) {
    if (!row(label, *v ? "On" : "Off", *v != def, desc)) return false;  // (any step flips it)
    *v = !*v;
    return true;
}

bool choice(const char* label, int* v, const char* const* names, int n, int def, const char* desc, bool wrap) {
    if(!names || n<=0) {disable_next();row(label,"",false,desc);return false;}
    const int cur = *v >= 0 && *v < n ? *v : 0;
    const int step = row(label, names[cur], cur != def, desc, true, true);
    if (!step) return false;
    int nv = cur + (step == 2 ? 1 : step);
    nv = wrap ? (nv + n) % n : std::clamp(nv, 0, n - 1);
    if (nv == cur) return false;
    *v = nv;
    return true;
}

bool integer(const char* label, int* v, int lo, int hi, int step, int def, const char* fmt, const char* desc) {
    char buf[48];
    std::snprintf(buf, sizeof buf, fmt, *v);
    const int s = row(label, buf, *v != def, desc, true, true);
    if (!s) return false;
    int n = *v + (s == 2 ? step : s * step);
    if (s == 2 && n > hi) n = lo;  // (A / click past the top wraps; left / right stop at the ends)
    n = std::clamp(n, lo, hi);
    if (n == *v) return false;
    *v = n;
    return true;
}

bool action(const char* label, const char* value, const char* desc, bool up, bool highlight) {
    return row(label, value ? value : "", false, desc, up, false, highlight) == 2;
}
bool primary_action(const char* label,const char* value,const char* desc) {
    return row(label,value?value:"",false,desc,false,false,false,true)==2;
}

void end() {
    ImGui::PopStyleVar();
    ImGui::EndChild();
    // the description: the hovered row (mouse) wins, else the selected one (controller), else the last shown
    // Keep a row's current description, not an old copy of its text. A language
    // change or removed row must not leave another language/stale mod details here.
    std::string want;
    if (!g_desc_hover.empty()) want = g_desc_hover, g_desc_id = g_desc_hover_id;
    else if (!g_desc_focus.empty()) want = g_desc_focus, g_desc_id = g_desc_focus_id;
    else if (!g_desc_retained.empty()) want = g_desc_retained;
    else want = g_desc_first, g_desc_id = g_desc_first_id;
    g_desc_shown=want; // Instant help changes for high-frequency keyboard navigation.
    const ImVec2 p(g_page_origin.x+(g_compact?0:g_panes.rows_width+g_panes.gap),
                   g_page_origin.y+(g_compact?g_panes.rows_height+g_panes.gap:0));
    auto* dl=ImGui::GetWindowDrawList();
    if(g_compact)dl->AddLine(ImVec2(p.x,p.y-g_panes.gap*.5f),ImVec2(p.x+g_panes.help_width,p.y-g_panes.gap*.5f),ui::fade(ui::theme().accent,.35f),std::max(1.f,g_k));
    else dl->AddLine(ImVec2(p.x-g_panes.gap*.5f,p.y),ImVec2(p.x-g_panes.gap*.5f,p.y+g_panes.help_height),ui::fade(ui::theme().accent,.35f),std::max(1.f,g_k));
    ImGui::SetCursorScreenPos(p);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(10*g_k,6*g_k));
    ImGui::BeginChild("##desc",ImVec2(g_panes.help_width,g_panes.help_height),ImGuiChildFlags_AlwaysUseWindowPadding,ImGuiWindowFlags_NoNav);
    ImGui::PopStyleVar();
    ImGui::PushTextWrapPos(0);ImGui::TextUnformatted(g_desc_shown.c_str());ImGui::PopTextWrapPos();
    if (!g_note.empty()) {
        ImGui::Dummy(ImVec2(0, 10 * g_k));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::theme().accent));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(g_note.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::PopID();
}

}  // namespace nx::overlay::rows
