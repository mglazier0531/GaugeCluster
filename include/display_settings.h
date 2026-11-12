#pragma once

struct TouchSample { bool down; int x, y; };
struct DisplaySettings {
    enum Change { None=0, Redraw=1, Backlight=2, Theme=4, Save=8 };
    bool open=false, night=false, pressed=false, dragging=false, dirty=false;
    int day_brightness=100, night_brightness=25;
    unsigned revision=1;
    int brightness() const { return night?night_brightness:day_brightness; }
    static bool inside(int x,int y,int l,int t,int r,int b) {
        return x>=l && x<=r && y>=t && y<=b;
    }
    unsigned touch(TouchSample p) {
        unsigned changes=None;
        bool tap=p.down && !pressed;
        pressed=p.down;
        if(!p.down) {
            if(dragging && dirty) changes|=Save;
            dragging=false; dirty=false;
            return changes;
        }
        if(!open) {
            if(tap && inside(p.x,p.y,932,15,1009,87)) { open=true; changes|=Redraw; }
        } else if(dragging || (tap && inside(p.x,p.y,282,245,742,315))) {
            dragging=true;
            int x=p.x<310?310:(p.x>714?714:p.x);
            int value=5+((x-310)*95+202)/404;
            int& current=night?night_brightness:day_brightness;
            if(current!=value) { current=value; dirty=true; changes|=Backlight|Redraw; }
        } else if(tap && inside(p.x,p.y,290,345,734,415)) {
            night=!night; changes|=Theme|Backlight|Save|Redraw;
        } else if(tap && (inside(p.x,p.y,724,142,780,198) ||
                           !inside(p.x,p.y,236,130,788,480))) {
            open=false; changes|=Redraw|Save;
        }
        if(changes&Redraw) revision++;
        return changes;
    }
};
