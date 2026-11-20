#pragma once
#include <algorithm>
#include <cstdint>

namespace nx::plat::menu_layout {
constexpr int width=720, height=680;
constexpr int binding_rows=8, binding_top=232, binding_stride=40, binding_height=34;
inline float unit(float font_size) {return std::max(.75f,font_size/22.f);}
inline float target_height(float font_size) {return std::max(36.f,40.f*unit(font_size));}
inline int columns(float available,float minimum,int count,float gap=0.f) {
    int columns=std::max(1,count);
    while(columns>1&&columns*std::max(1.f,minimum)+(columns-1)*std::max(0.f,gap)>available)--columns;
    return columns;
}
struct Rect {int left,top,right,bottom;};
struct Placement {int x,y,width,height;};
inline Placement place(Rect owner,Rect work,unsigned dpi) {
    const int available_w=std::max(1,work.right-work.left-32);
    const int available_h=std::max(1,work.bottom-work.top-32);
    const double preferred=std::clamp(dpi/96.0,1.0,3.0);
    const double scale=std::min({preferred,double(available_w)/width,double(available_h)/height});
    const int w=std::max(1,int(width*scale)),h=std::max(1,int(height*scale));
    const int x=std::clamp(owner.left+(owner.right-owner.left-w)/2,
        work.left,work.right-w);
    const int y=std::clamp(owner.top+(owner.bottom-owner.top-h)/2,
        work.top,work.bottom-h);
    return {x,y,w,h};
}
inline int logical(int pixel,int actual,int extent) {
    return actual>0?int(int64_t(pixel)*extent/actual):0;
}
inline int binding_at(int y,int page) {
    const int offset=y-binding_top;
    if(offset<0 || offset>=binding_rows*binding_stride ||
       offset%binding_stride>=binding_height || page<0 || page>1) return -1;
    return page*binding_rows+offset/binding_stride;
}
inline int popup_top(int anchor_bottom,int count,int row_height,int min_top,int max_bottom) {
    const int h=count*row_height;
    return std::clamp(anchor_bottom+2,min_top,std::max(min_top,max_bottom-h));
}
}
