#include "overlay/layout_geometry.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace nx::overlay::ui::layout;
static void check(bool ok,const char* why) {if(!ok){std::fprintf(stderr,"overlay layout: %s\n",why);std::exit(1);}}
int main() {
    for(float width:{1.f,40.f,120.f,260.f,480.f,760.f,1280.f,3840.f})for(float scale:{.5f,1.f,1.5f,3.f})for(float font:{14.f,29.f,58.f,100.f}) {
        const auto panes_layout=panes(width,120,scale,font);
        check(panes_layout.rows_width>0 && panes_layout.help_width>0 && panes_layout.rows_height>0 && panes_layout.help_height>0,"panes stay positive in narrow/large-text windows");
        if(panes_layout.compact)check(panes_layout.rows_height+panes_layout.help_height+panes_layout.gap<=120.01f,"stacked help never extends beyond available height");
        else check(panes_layout.rows_width+panes_layout.help_width+panes_layout.gap<=width+.01f,"two panes never extend beyond available width");
        for(bool arrows:{false,true})for(float label:{12.f,200.f,1200.f})for(float value:{0.f,40.f,2000.f}) {
            const auto c=row_columns(width,scale,font,label,value,value>0,arrows);
            check(c.label_width>0 && c.control_x>=0 && c.control_x+c.control_width<=width+.01f,"label/value columns stay within row");
            check(c.value_width>=0 && c.arrow_width>=0 && std::isfinite(c.value_width),"narrow control sizes are finite");
            if(value>0)check(c.value_width+2*c.arrow_width<=c.control_width+.01f,"arrows do not overlap the value's text width");
            const auto one=row_vertical(c,scale,font,font,font,value>0);
            const auto wrapped=row_vertical(c,scale,font,font*3,font*5,value>0);
            check(one.height>=36 && wrapped.height>=one.height,"wrapped content grows row instead of shrinking font");
            check(wrapped.label_y>=0 && wrapped.label_y+font*3<=wrapped.height+.01f,"wrapped label fits computed row");
            if(value>0)check(wrapped.control_y+wrapped.control_height<=wrapped.height+.01f,"wrapped value fits computed row");
        }
        check(button_width(width,400,600,16)<=width,"requested action width clamps to narrow content");
        check(button_width(width,0,600,16)<=width && button_width(width,-1,20,16)==width,"auto and fill action widths stay bounded");
    }
    const auto packed=flow({{200,40},{200,60},{300,50}},450,20,12);
    check(packed.items.size()==3 && std::fabs(packed.height-122)<.01f,"wrapped footer reserves full line heights and gap");
    check(std::fabs(packed.items[0].y-10)<.01f && packed.items[1].y==0 && packed.items[2].y>=72,"mixed badge/text group heights align without overlap");
    for(float available:{1.f,32.f,120.f,450.f,1920.f}) {
        const auto wrapped=flow({{40,32},{80,32},{600,120},{80,48},{200,72}},available,12,10);
        for(size_t i=0;i<wrapped.items.size();++i) {
            const auto& item=wrapped.items[i];
            check(item.x>=0 && item.x+item.width<=available+.01f,"footer groups remain inside explicit width");
            check(item.y>=0 && item.y+item.height<=wrapped.height+.01f,"all wrapped groups fit measured footer height");
            for(size_t j=0;j<i;++j) {
                const auto& prior=wrapped.items[j];
                const bool separate=item.x>=prior.x+prior.width || prior.x>=item.x+item.width ||
                    item.y>=prior.y+prior.height || prior.y>=item.y+item.height;
                check(separate,"wrapped footer badge/text groups do not overlap");
            }
        }
    }
    check(flow({},200,12,10).height==0,"empty footer reserves no space");
    const auto large=row_columns(300,1,29,600,900,true,true);
    check(large.stacked && large.value_width>0,"long translations use stacked wrapping");
    check(std::isfinite(panes(std::numeric_limits<float>::infinity(),120,1,29).rows_width),"nonfinite input fails to bounded geometry");
    std::puts("Overlay layout: narrow/DPI/long-label pane bounds, wrapped row heights action widths and wrapped footer flow passed");
}
