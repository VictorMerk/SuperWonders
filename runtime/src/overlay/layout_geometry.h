#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace nx::overlay::ui::layout {
inline float finite_positive(float value,float fallback=1.f) {return std::isfinite(value)&&value>0?value:fallback;}
struct RowColumns {
    float padding=0,gap=0,inner=0,label_width=0,control_x=0,control_width=0,value_width=0,arrow_width=0;
    bool stacked=false;
};
inline RowColumns row_columns(float width,float scale,float font,float label,float value,bool has_value,bool arrows) {
    width=finite_positive(width);scale=finite_positive(scale);font=finite_positive(font);
    RowColumns r;
    r.padding=std::min(std::max(8.f,16*scale),width*.15f);
    r.inner=std::max(.01f,width-2*r.padding);
    r.gap=std::min(std::max(6.f,10*scale),r.inner*.06f);
    r.arrow_width=has_value&&arrows?std::min(std::max(32.f,font*1.1f),r.inner*.22f):0;
    const float reserve=arrows&&has_value?2*(r.arrow_width+r.gap):0;
    r.control_width=has_value?std::min(r.inner,std::max(0.f,value)+reserve):0;
    r.stacked=has_value && std::max(0.f,label)+r.control_width+r.gap>r.inner;
    if(r.stacked)r.control_width=r.inner;
    r.label_width=has_value&&!r.stacked?std::max(.01f,r.inner-r.control_width-r.gap):r.inner;
    r.value_width=has_value?std::max(.01f,r.control_width-reserve):0;
    r.control_x=r.padding+r.inner-r.control_width;
    return r;
}
struct RowVertical {float height=0,label_y=0,control_y=0,control_height=0;};
inline RowVertical row_vertical(const RowColumns& columns,float scale,float font,float label_height,float value_height,bool has_value) {
    scale=finite_positive(scale);font=finite_positive(font);
    const float pad=std::max(6.f,8*scale),gap=std::max(4.f,5*scale);
    RowVertical r;
    r.control_height=has_value?std::max(value_height,columns.arrow_width):0;
    const float content=columns.stacked?label_height+gap+r.control_height:std::max(label_height,r.control_height);
    r.height=std::max(std::max(36.f,font*1.8f),content+2*pad);
    r.label_y=columns.stacked?pad:(r.height-label_height)*.5f;
    r.control_y=columns.stacked?pad+label_height+gap:(r.height-r.control_height)*.5f;
    return r;
}
struct Panes {float rows_width=0,rows_height=0,help_width=0,help_height=0,gap=0;bool compact=false;};
inline Panes panes(float width,float height,float scale,float font) {
    width=finite_positive(width);height=finite_positive(height);scale=finite_positive(scale);font=finite_positive(font);
    Panes p;p.compact=width<std::max(760*scale,font*26);
    if(p.compact) {
        p.gap=std::min(12*scale,height*.04f);
        p.help_height=std::min(std::max(96*scale,font*4),height*.34f);
        p.rows_height=height-p.gap-p.help_height;p.rows_width=p.help_width=width;
    } else {
        p.gap=std::min(26*scale,width*.08f);p.rows_width=width*.6f;p.help_width=width-p.rows_width-p.gap;
        p.rows_height=p.help_height=height;
    }
    return p;
}
inline float button_width(float available,float requested,float natural,float padding) {
    available=finite_positive(available);
    const float wanted=requested<0?available:requested>0?requested:std::max(96.f,natural+2*padding);
    return std::min(available,std::max(1.f,wanted));
}
struct FlowExtent {float width=0,height=0;};
struct FlowItem {float x=0,y=0,width=0,height=0;};
struct Flow {std::vector<FlowItem> items;float height=0;};
// Greedy whole-group wrapping. Each line is centered; items align vertically
// within that line. Text wrapping happens before this, at normal font size.
inline Flow flow(const std::vector<FlowExtent>& extents,float max_width,float gap,float line_gap) {
    max_width=finite_positive(max_width);gap=std::max(0.f,gap);line_gap=std::max(0.f,line_gap);
    Flow result;size_t first=0;float used=0,line_height=0,y=0;
    const auto finish=[&] {
        const float offset=(max_width-used)*.5f;
        for(size_t i=first;i<result.items.size();++i) {
            auto& item=result.items[i];item.x+=offset;item.y=y+(line_height-item.height)*.5f;
        }
        y+=line_height;first=result.items.size();used=0;line_height=0;
    };
    for(const auto& extent:extents) {
        const float width=std::min(max_width,std::max(0.f,extent.width)),height=std::max(0.f,extent.height);
        if(result.items.size()>first && used+gap+width>max_width) {finish();y+=line_gap;}
        const float x=used+(result.items.size()>first?gap:0);
        result.items.push_back({x,0,width,height});used=x+width;line_height=std::max(line_height,height);
    }
    if(result.items.size()>first)finish();
    result.height=y;return result;
}

}
