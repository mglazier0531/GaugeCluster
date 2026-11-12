#pragma once
#include "gauge_limits.h"

struct GaugeSlot { int left,top,width,height; };
constexpr GaugeSlot gauge_slots[5]={
    {302,90,410,431},{56,86,248,230},{56,305,248,230},
    {716,86,248,230},{715,305,250,230}
};
constexpr Gauge default_gauges[5]={Gauge::Rpm,Gauge::Temperature,Gauge::Oil,Gauge::Fuel,Gauge::Voltage};
constexpr int slot_at(int x,int y) {
    for(int i=0;i<5;i++) {
        auto r=gauge_slots[i];
        float nx=(x-r.left-r.width*.5f)/(r.width*.5f);
        float ny=(y-r.top-r.height*.5f)/(r.height*.5f);
        if(nx*nx+ny*ny<=1) return i;
    }
    return -1;
}
constexpr int picker_choice_at(int x,int y) {
    for(int i=0;i<10;i++) {
        int left=222+(i%2)*304,top=158+(i/2)*66;
        if(x>=left && x<=left+276 && y>=top && y<=top+54) return i;
    }
    return -1;
}
