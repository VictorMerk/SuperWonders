#include "pc_menu_layout.h"
#include <array>
#include <cstdlib>
#include <iostream>
using namespace nx::plat::menu_layout;
void check(bool value,const char* reason) {
    if(!value){std::cerr<<reason<<'\n';std::exit(1);}
}
int main() {
    for(Rect work:std::array<Rect,5>{{{0,0,1920,1040},{0,0,1280,680},
        {-1920,-120,0,960},{300,200,940,680},{0,0,100,100}}}) {
        for(unsigned dpi:{96u,144u,192u,288u}) {
            for(Rect owner:std::array<Rect,3>{{{0,0,1920,1080},{-4000,-2000,-2000,-1000},
                {5000,3000,6000,4000}}}) {
                const auto p=place(owner,work,dpi);
                check(p.width>0&&p.height>0,"positive drawable area");
                check(p.x>=work.left&&p.y>=work.top&&p.x+p.width<=work.right&&p.y+p.height<=work.bottom,
                    "settings never land outside monitor work area");
                const int error=p.width*height-p.height*width;
                check(error>-height&&error<width,"DPI fit preserves logical aspect within rounding");
                check(logical(p.width,p.width,width)==width&&logical(p.height,p.height,height)==height,
                    "painting and hit-test edges share logical coordinates");
            }
        }
    }
    for(int page=0;page<2;++page) for(int row=0;row<binding_rows;++row) {
        const int top=binding_top+row*binding_stride;
        check(binding_at(top,page)==page*8+row,"all16 bindings reachable");
        check(binding_at(top+binding_height-1,page)==page*8+row,"full visible binding height clickable");
        check(binding_at(top+binding_height,page)==-1,"row gaps cannot capture another action");
    }
    check(binding_at(binding_top-1,0)==-1&&binding_at(binding_top,2)==-1,"invalid row/page cannot bind");
    check(columns(616,200,3,8)==3,"toolbar fits exact row with gaps");
    check(columns(615,200,3,8)==2,"toolbar wraps before targets shrink");
    check(columns(100,200,5,8)==1,"narrow localized navigation remains reachable");
    check(columns(2000,100,5,8)==5,"wide navigation uses one row");
    for(float font:{12.f,18.f,24.f,44.f})check(target_height(font)>=36&&target_height(font)>=font,
        "comfortable target height follows font size");
    std::cout<<"Native settings layout: work areas, DPI fit, inverse hit tests, all bindings, gaps and responsive navigation passed\n";
}
