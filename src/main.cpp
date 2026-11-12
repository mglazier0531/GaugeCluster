#include "rgb_lcd_port.h"
#include "dashboard_assets.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/semphr.h"
#include "touch_input.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "gauge_limits.h"
#include "selectable_assets.h"
#include "hidden_alerts.h"
#include <cmath>
#include <cstdio>
#include <cstring>

// Rendering is independent of the source of engine data. NMEA 2000 comes next.
struct EngineData { float rpm, temperature, oil, fuel, voltage, speed, hours, flow, trim,afr,intake_air,accelerator,fuel_pressure; };
static uint16_t *frame;
static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t vsync;
static DisplaySettings settings;
static HiddenAlerts hidden_alerts;
static uint16_t *day_background,*night_background,*night_palette;
struct CustomTrack { BandPixel* pixels=nullptr; unsigned count=0; uint32_t* outline=nullptr; unsigned outline_count=0; };
static CustomTrack custom_tracks[5];
static const uint16_t* background=dashboard_background;
static uint16_t themed(uint16_t color) { return settings.night?night_palette[color]:color; }
static void prepare_night_palette() {
    night_palette=static_cast<uint16_t*>(heap_caps_malloc(65536*2,MALLOC_CAP_SPIRAM));
    night_background=static_cast<uint16_t*>(heap_caps_malloc(1024*600*2,MALLOC_CAP_SPIRAM));
    day_background=static_cast<uint16_t*>(heap_caps_malloc(1024*600*2,MALLOC_CAP_SPIRAM));
    assert(night_palette && night_background && day_background);
    for(unsigned c=0;c<65536;c++) {
        unsigned r=((c>>11)&31)*255/31,g=((c>>5)&63)*255/63,b=(c&31)*255/31;
        unsigned l=(r*30+g*59+b*11)/100;
        unsigned nr=l*3/4,ng=l/5,nb=l/32;
        if(r>g*2 && r>b*2) { nr=r*3/4; ng=g/6; nb=0; }
        night_palette[c]=((nr>>3)<<11)|((ng>>2)<<5)|(nb>>3);
    }
    for(unsigned i=0;i<1024*600;i++) night_background[i]=night_palette[dashboard_background[i]];
}
static uint16_t face_sample(unsigned kind,int x,int y,int width,int height) {
    unsigned fx=unsigned(x)*(gauge_face_size-1)*256/(width-1),fy=unsigned(y)*(gauge_face_size-1)*256/(height-1);
    unsigned sx=fx>>8,sy=fy>>8,tx=sx+1<gauge_face_size?sx+1:sx,ty=sy+1<gauge_face_size?sy+1:sy;
    const uint16_t* src=gauge_face_pixels+kind*gauge_face_size*gauge_face_size;
    uint16_t colors[]={src[sy*gauge_face_size+sx],src[sy*gauge_face_size+tx],src[ty*gauge_face_size+sx],src[ty*gauge_face_size+tx]};
    unsigned a=fx&255,b=fy&255,weights[]={(256-a)*(256-b),a*(256-b),(256-a)*b,a*b};
    unsigned red=0,green=0,blue=0;
    for(unsigned i=0;i<4;i++) { red+=((colors[i]>>11)&31)*weights[i]; green+=((colors[i]>>5)&63)*weights[i]; blue+=(colors[i]&31)*weights[i]; }
    return ((red>>16)<<11)|((green>>16)<<5)|(blue>>16);
}
static void rebuild_composition() {
    memcpy(day_background,dashboard_background,1024*600*2);
    auto* lookup=static_cast<uint16_t*>(heap_caps_malloc(gauge_face_size*gauge_face_size*2,MALLOC_CAP_SPIRAM));
    assert(lookup);
    for(unsigned slot=0;slot<5;slot++) {
        auto& cache=custom_tracks[slot];
        free(cache.pixels); free(cache.outline); cache=CustomTrack{};
        if(settings.gauges[slot]==default_gauges[slot]) continue;
        unsigned kind=static_cast<unsigned>(settings.gauges[slot]);
        auto rect=gauge_slots[slot];
        memset(lookup,0xff,gauge_face_size*gauge_face_size*2);
        unsigned first=gauge_face_ranges[kind],last=gauge_face_ranges[kind+1];
        assert(last-first<65535);
        for(unsigned i=first;i<last;i++) lookup[gauge_face_bands[i].offset]=i-first;
        for(int y=0;y<rect.height;y++) for(int x=0;x<rect.width;x++) {
            float nx=(x-rect.width*.5f)/(rect.width*.5f),ny=(y-rect.height*.5f)/(rect.height*.5f);
            float radius=nx*nx+ny*ny;
            unsigned source=(y*gauge_face_size/rect.height)*gauge_face_size+x*gauge_face_size/rect.width;
            if(lookup[source]!=65535) cache.count++;
            if(radius>=.93f*.93f && radius<=.945f*.945f) cache.outline_count++;
            if(radius<=1) day_background[(rect.top+y)*1024+rect.left+x]=face_sample(kind,x,y,rect.width,rect.height);
        }
        cache.pixels=static_cast<BandPixel*>(heap_caps_malloc(cache.count*sizeof(BandPixel),MALLOC_CAP_SPIRAM));
        cache.outline=static_cast<uint32_t*>(heap_caps_malloc(cache.outline_count*4,MALLOC_CAP_SPIRAM));
        assert(cache.pixels && cache.outline);
        unsigned n=0,m=0;
        for(int y=0;y<rect.height;y++) for(int x=0;x<rect.width;x++) {
            unsigned source=(y*gauge_face_size/rect.height)*gauge_face_size+x*gauge_face_size/rect.width;
            unsigned offset=(rect.top+y)*1024+rect.left+x;
            if(lookup[source]!=65535) { cache.pixels[n]=gauge_face_bands[first+lookup[source]]; cache.pixels[n++].offset=offset; }
            float nx=(x-rect.width*.5f)/(rect.width*.5f),ny=(y-rect.height*.5f)/(rect.height*.5f);
            float radius=nx*nx+ny*ny;
            if(radius>=.93f*.93f && radius<=.945f*.945f) cache.outline[m++]=offset;
        }
    }
    free(lookup);
    for(unsigned i=0;i<1024*600;i++) night_background[i]=night_palette[day_background[i]];
}
static void load_settings() {
    esp_err_t err=nvs_flash_init();
    if(err==ESP_ERR_NVS_NO_FREE_PAGES || err==ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase()); err=nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    nvs_handle_t nvs;
    if(nvs_open("display",NVS_READONLY,&nvs)==ESP_OK) {
        int32_t value;
        if(nvs_get_i32(nvs,"day",&value)==ESP_OK && value>=5 && value<=100) settings.day_brightness=value;
        if(nvs_get_i32(nvs,"night_level",&value)==ESP_OK && value>=5 && value<=100) settings.night_brightness=value;
        uint8_t night=0;
        if(nvs_get_u8(nvs,"night",&night)==ESP_OK) settings.night=night==1;
        uint8_t ids[5]; size_t length=sizeof(ids);
        if(nvs_get_blob(nvs,"gauges",ids,&length)==ESP_OK && length==sizeof(ids))
            for(unsigned i=0;i<5;i++) if(ids[i]<gauge_count) settings.gauges[i]=static_cast<Gauge>(ids[i]);
        nvs_close(nvs);
    }
}
static void save_settings() {
    nvs_handle_t nvs;
    esp_err_t err=nvs_open("display",NVS_READWRITE,&nvs);
    if(err==ESP_OK) {
        err=nvs_set_i32(nvs,"day",settings.day_brightness);
        if(err==ESP_OK) err=nvs_set_i32(nvs,"night_level",settings.night_brightness);
        if(err==ESP_OK) err=nvs_set_u8(nvs,"night",settings.night);
        uint8_t ids[5]; for(unsigned i=0;i<5;i++) ids[i]=static_cast<uint8_t>(settings.gauges[i]);
        if(err==ESP_OK) err=nvs_set_blob(nvs,"gauges",ids,sizeof(ids));
        if(err==ESP_OK) err=nvs_commit(nvs);
        nvs_close(nvs);
    }
    if(err!=ESP_OK) ESP_LOGE("settings","Save failed: %s",esp_err_to_name(err));
}
static void set_backlight() {
    IO_EXTENSION_Pwm_Output(100-settings.brightness());
    ESP_LOGI("settings","%s mode, backlight %d%%",settings.night?"Night":"Day",settings.brightness());
}
static bool IRAM_ATTR on_vsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*) {
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(vsync, &wake);
    return wake == pdTRUE;
}
static void pixel(int x,int y,uint16_t c) {
    if(x>=0 && x<1024 && y>=0 && y<600) frame[y*1024+x]=c;
}
static void restore(int x0,int y0,int x1,int y1) {
    y0=int(y0*dashboard_y_scale); y1=int(y1*dashboard_y_scale);
    for(int y=y0;y<y1;y++) {
        if(y<0 || y>=600) continue;
        int left=x0<0?0:x0, right=x1>1024?1024:x1;
        if(right>left) memcpy(frame+y*1024+left,background+y*1024+left,(right-left)*2);
    }
}
static void restore_physical(int x0,int y0,int x1,int y1) {
    for(int y=y0;y<y1;y++) memcpy(frame+y*1024+x0,background+y*1024+x0,(x1-x0)*2);
}
static void text(const char* str,int cx,int top,int height,int maxwidth,bool center_vertical=false,uint16_t color=0xffff) {
    uint16_t ink=themed(color);
    float scale=height/100.0f;
    int sum=0;
    for(const char* p=str;*p;p++) { const char* q=strchr(font_chars,*p); if(q) sum+=font_widths[q-font_chars]; }
    if(sum*scale>maxwidth) scale=float(maxwidth)/sum;
    // Arial atlas visible glyph bounds are y=19..92; center the actual ink,
    // accounting for width fitting (one- and two-digit speeds differ in width).
    if(center_vertical) top-=int(55.5f*scale);
    int left=cx-int(sum*scale)/2;
    const int stride=96*(sizeof(font_chars)-1);
    for(const char* p=str;*p;p++) {
        const char* q=strchr(font_chars,*p); if(!q) continue;
        int index=q-font_chars;
        for(int y=0;y<int(120*scale*dashboard_y_scale);y++) for(int x=0;x<int(96*scale);x++) {
            int sx=int(x/scale), sy=int(y/(scale*dashboard_y_scale));
            unsigned a=font_alpha[sy*stride+index*96+sx];
            int dx=left+x,dy=int(top*dashboard_y_scale)+y;
            if(!a || dx<0 || dx>=1024 || dy<0 || dy>=600) continue;
            uint16_t bg=frame[dy*1024+dx];
            unsigned r=(((bg>>11)&31)*(255-a)+((ink>>11)&31)*a)/255;
            unsigned g=(((bg>>5)&63)*(255-a)+((ink>>5)&63)*a)/255;
            unsigned b=((bg&31)*(255-a)+(ink&31)*a)/255;
            pixel(dx,dy,(r<<11)|(g<<5)|b);
        }
        left+=int(font_widths[index]*scale);
    }
}
static uint16_t alert_ink(Alert alert,bool flash_on) {
    if(alert==Alert::Warning) return 0xffe0;
    if(alert==Alert::Critical && flash_on) return 0xf800;
    return 0xffff;
}
static void band(unsigned gauge,float fraction,Alert alert,bool flash_on) {
    unsigned cutoff=unsigned(lroundf(65535*fminf(1,fmaxf(0,fraction))));
    for(unsigned i=band_ranges[gauge];i<band_ranges[gauge+1];i++) {
        const auto& p=band_pixels[i];
        bool filled=cutoff>0 && p.threshold<=cutoff;
        uint16_t color=filled?p.active:p.inactive;
        if(alert==Alert::Warning) color=filled?0xffe0:0x39a0;
        if(alert==Alert::Critical && flash_on) color=filled?0xf800:0x9800;
        frame[p.offset]=themed(color);
    }
    bool highlight=alert==Alert::Warning || (alert==Alert::Critical && flash_on);
    uint16_t color=themed(alert==Alert::Warning?0xffe0:0xf800);
    for(unsigned i=highlight_ranges[gauge];i<highlight_ranges[gauge+1];i++) {
        unsigned offset=highlight_pixels[i];
        frame[offset]=highlight?color:background[offset];
    }
}
static void format_gauge_value(Gauge gauge,float value,char* value_text,size_t size) {
    switch(gauge) {
        case Gauge::Rpm:
            if(value<1000) snprintf(value_text,size,"%d",int(value));
            else snprintf(value_text,size,"%d,%03d",int(value)/1000,int(value)%1000);
            break;
        case Gauge::Temperature: case Gauge::IntakeAir: snprintf(value_text,size,"%.0f F",value); break;
        case Gauge::Oil: case Gauge::FuelPressure: snprintf(value_text,size,"%.0f PSI",value); break;
        case Gauge::Voltage: snprintf(value_text,size,"%.1f V",value); break;
        case Gauge::Afr: snprintf(value_text,size,"%.1f",value); break;
        default: snprintf(value_text,size,"%.0f%%",value); break;
    }
}
static void custom_gauge(unsigned slot,Gauge gauge,float value,Alert alert,bool flash_on) {
    unsigned kind=static_cast<unsigned>(gauge);
    const auto& rect=gauge_slots[slot];
    const auto& spec=gauge_face_specs[kind];
    auto& cache=custom_tracks[slot];
    restore_physical(rect.left+rect.width*12/100,rect.top+rect.height*155/320,
                     rect.left+rect.width*88/100,rect.top+rect.height*240/320);
    unsigned cutoff=unsigned(lroundf(65535*fminf(1,fmaxf(0,(value-spec.minimum)/(spec.maximum-spec.minimum)))));
    for(unsigned i=0;i<cache.count;i++) {
        auto& p=cache.pixels[i]; bool filled=cutoff>0 && p.threshold<=cutoff;
        uint16_t color=filled?p.active:p.inactive;
        if(alert==Alert::Warning) color=filled?0xffe0:0x39a0;
        if(alert==Alert::Critical && flash_on) color=filled?0xf800:0x9800;
        frame[p.offset]=themed(color);
    }
    bool highlight=alert==Alert::Warning || (alert==Alert::Critical && flash_on);
    for(unsigned i=0;i<cache.outline_count;i++) {
        unsigned offset=cache.outline[i];
        frame[offset]=highlight?themed(alert==Alert::Warning?0xffe0:0xf800):background[offset];
    }
    char value_text[40];
    format_gauge_value(gauge,value,value_text,sizeof(value_text));
    int center_y=rect.top+rect.height*190/320;
    text(value_text,rect.left+rect.width/2,int(center_y/dashboard_y_scale),rect.width*17/100,rect.width*68/100,true,alert_ink(alert,flash_on));
}
static void rounded_rect(int l,int t,int r,int b,int radius,uint16_t color) {
    color=themed(color);
    for(int y=t;y<b;y++) for(int x=l;x<r;x++) {
        int dx=x<l+radius?l+radius-x:(x>=r-radius?x-(r-radius-1):0);
        int dy=y<t+radius?t+radius-y:(y>=b-radius?y-(b-radius-1):0);
        if(dx*dx+dy*dy<=radius*radius) pixel(x,y,color);
    }
}
static void ui_text(const char* value,int cx,int center_y,int height,int maxwidth,uint16_t color=0xffff) {
    text(value,cx,int(center_y/dashboard_y_scale),height,maxwidth,true,color);
}
static void settings_popup() {
    rounded_rect(236,130,788,480,18,0x07ff);
    rounded_rect(239,133,785,477,15,0x0862);
    ui_text("DISPLAY SETTINGS",474,171,29,380);
    rounded_rect(725,145,777,195,10,0x2104);
    ui_text("X",750,170,29,40);
    ui_text("BACKLIGHT",391,225,25,230);
    char value[12]; snprintf(value,sizeof(value),"%d%%",settings.brightness());
    ui_text(value,686,225,29,95);
    rounded_rect(310,274,714,286,6,0x39c7);
    int knob=310+(settings.brightness()-5)*404/95;
    if(knob>310) rounded_rect(310,274,knob+1,286,6,0x07ff);
    rounded_rect(knob-17,263,knob+18,298,17,0xffff);
    rounded_rect(knob-10,270,knob+11,291,10,0x07ff);
    ui_text("5%",310,320,16,80); ui_text("100%",714,320,16,80);
    rounded_rect(290,345,734,415,12,settings.night?0xc3e0:0x07ff);
    rounded_rect(292,347,732,413,10,0x1082);
    ui_text(settings.night?"NIGHT MODE  ON":"NIGHT MODE  OFF",512,380,28,405);
    ui_text("DAY AND NIGHT BRIGHTNESS SAVED",512,448,17,500);
}
static void gauge_picker() {
    rounded_rect(195,90,829,520,18,0x07ff);
    rounded_rect(198,93,826,517,15,0x0862);
    ui_text("SELECT GAUGE",475,123,29,430);
    rounded_rect(746,100,804,146,10,0x2104); ui_text("X",775,123,28,45);
    for(unsigned i=0;i<gauge_count;i++) {
        int left=222+(i%2)*304,top=158+(i/2)*66;
        bool selected=settings.gauges[settings.chooser_slot]==static_cast<Gauge>(i);
        rounded_rect(left,top,left+276,top+54,9,selected?0x07ff:0x39c7);
        rounded_rect(left+2,top+2,left+274,top+52,7,selected?0x0248:0x1082);
        ui_text(gauge_face_specs[i].name,left+138,top+27,22,252);
    }
}
static void hidden_alert_popup(const float (&values)[gauge_count]) {
    bool flash=(esp_timer_get_time()/500000)%2==0;
    uint16_t color=hidden_alerts.critical_mask?0xf800:0xffe0;
    rounded_rect(215,105,809,505,18,flash?color:0x4208);
    rounded_rect(219,109,805,501,14,0x0862);
    ui_text("HIDDEN GAUGE ALERTS",512,140,30,540,flash?color:0xffff);
    ui_text("GAUGES NOT CURRENTLY DISPLAYED",512,173,17,540);
    unsigned row=0;
    for(int severity=2;severity>=1;severity--) for(unsigned i=0;i<gauge_count;i++) {
        if(!(hidden_alerts.mask&(1u<<i)) || static_cast<int>(hidden_alerts.levels[i])!=severity) continue;
        char value[40]; format_gauge_value(static_cast<Gauge>(i),values[i],value,sizeof(value));
        uint16_t ink=severity==2?(flash?0xf800:0xffff):0xffe0;
        ui_text(gauge_face_specs[i].name,415,209+row*25,20,360,ink);
        ui_text(value,716,209+row*25,22,135,ink);
        row++;
    }
    rounded_rect(327,445,697,489,10,color);
    rounded_rect(329,447,695,487,8,0x1082);
    ui_text("ACKNOWLEDGE",512,467,25,335);
}
static EngineData demo(float t) {
    float wave=0.5f-0.5f*cosf(t*0.28f);
    EngineData data={800+4200*wave,176+6*sinf(t*.12f),30+35*wave,
            72+2*sinf(t*.04f),13.8f+.25f*sinf(t*.3f),
            3+42*wave,487.6f+t/3600,1.2f+18*wave,
            35+8*sinf(t*.13f),14.2f+.3f*sinf(t*.18f),108+4*sinf(t*.11f),42+10*sinf(t*.14f),58+4*sinf(t*.15f)};
    // 84-second cycle demonstrates every condition and recovery, without hiding
    // the regular RPM sweep. Each scenario ramps in/out over two seconds.
    float cycle=fmodf(t,84.0f),within=fmodf(cycle,12.0f);
    int scenario=int(cycle/12);
    float blend=fminf(1.0f,fminf(within/2,(12-within)/2));
    if(scenario==1) data.temperature+=(210-data.temperature)*blend;
    if(scenario==2) data.oil+=(10-data.oil)*blend;
    if(scenario==3) data.fuel+=(16-data.fuel)*blend;
    if(scenario==4) data.fuel+=(7-data.fuel)*blend;
    if(scenario==5) data.voltage+=(9.5f-data.voltage)*blend;
    if(scenario==6) {
        data.trim+=(70-data.trim)*blend;
        data.afr+=(17.2f-data.afr)*blend;
        data.intake_air+=(145-data.intake_air)*blend;
        data.accelerator+=(88-data.accelerator)*blend;
        data.fuel_pressure+=(20-data.fuel_pressure)*blend;
    }
    if(scenario==0 && within<3) { data.rpm=0; data.trim=75; }
    return data;
}
static void render(const EngineData& d) {
    bool flash_on=(esp_timer_get_time()/500000)%2==0;
    const float values[]={d.rpm,d.temperature,d.oil,d.fuel,d.voltage,d.trim,d.afr,d.intake_air,d.accelerator,d.fuel_pressure};
    // Only restore changing regions; full-frame flash-to-PSRAM copies compete with LCD DMA.
    const int areas[][4]={{390,250,640,345},{135,186,240,238},{140,382,240,433},
        {780,168,910,222},{775,382,914,434},{676,21,848,76},
        {169,515,321,558},{474,515,635,558}};
    for(auto& a:areas) restore(a[0],a[1],a[2],a[3]);
    char s[40];
    for(unsigned slot=0;slot<5;slot++) {
        Gauge gauge=settings.gauges[slot]; unsigned kind=static_cast<unsigned>(gauge);
        float value=values[kind]; Alert alert=gauge_alert(gauge,value,d.rpm);
        uint16_t color=alert_ink(alert,flash_on);
        if(gauge!=default_gauges[slot]) { custom_gauge(slot,gauge,value,alert,flash_on); continue; }
        switch(slot) {
            case 0:
                if(value<1000) snprintf(s,sizeof(s),"%d",int(value));
                else snprintf(s,sizeof(s),"%d,%03d",int(value)/1000,int(value)%1000);
                text(s,511,250,76,226,false,color); break;
            case 1:
                snprintf(s,sizeof(s),"%.0f",value); text(s,175,186,43,80,false,color); text("F",224,194,24,20,false,color); break;
            case 2:
                snprintf(s,sizeof(s),"%.0f",value); text(s,171,382,43,70,false,color); text("PSI",216,397,21,40,false,color); break;
            case 3:
                snprintf(s,sizeof(s),"%.0f%%",value); text(s,841,168,45,114,false,color); break;
            case 4:
                snprintf(s,sizeof(s),"%.1f",value); text(s,829,382,43,99,false,color); text("V",889,399,24,24,false,color); break;
        }
        const auto& spec=gauge_face_specs[kind];
        band(slot,(value-spec.minimum)/(spec.maximum-spec.minimum),alert,flash_on);
    }
    snprintf(s,sizeof(s),"%.1f MPH",d.speed); text(s,760,48,42,160,true);
    snprintf(s,sizeof(s),"%.1f h",d.hours); text(s,243,515,35,142);
    snprintf(s,sizeof(s),"%.1f GPH",d.flow); text(s,554,515,35,150);
}
extern "C" void app_main() {
    printf("GaugeCluster: 1024x600 ANIMATED DEMO; NMEA2000 disabled\n");
    load_settings(); prepare_night_palette(); rebuild_composition();
    for(unsigned i=0;i<5;i++) ESP_LOGI("layout","Loaded slot %u = %s",i,gauge_face_specs[static_cast<unsigned>(settings.gauges[i])].name);
    background=settings.night?night_background:day_background;
    DEV_I2C_Port port=DEV_I2C_Init(); IO_EXTENSION_Init();
    IO_EXTENSION_Output(IO_EXTENSION_IO_2,0);
    IO_EXTENSION_Output(IO_EXTENSION_IO_6,1);
    touch_input_init(port.bus);
    set_backlight();
    panel=waveshare_esp32_s3_rgb_lcd_init();
    vsync=xSemaphoreCreateBinary();
    esp_lcd_rgb_panel_event_callbacks_t cb={}; cb.on_vsync=on_vsync;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel,&cb,nullptr));
    void *a,*b; waveshare_get_frame_buffer(&a,&b);
    auto *buffers0=static_cast<uint16_t*>(a); auto *buffers1=static_cast<uint16_t*>(b);
    memcpy(buffers0,background,1024*600*2);
    memcpy(buffers1,background,1024*600*2);
    unsigned previous_overlay[2]={0,0};
    bool previous_night[2]={settings.night,settings.night};
    unsigned composition_revision=1,previous_composition[2]={1,1};
    bool next=false;
    Alert previous_alerts[gauge_count]={};
    bool trim_interlock_logged=false;
    bool alarm_touch_capture=false,alarm_pressed=false;
    for(unsigned n=0;;n++) {
        TouchSample touch;
        unsigned changes=0;
        while(touch_input_next(touch)) {
            if(hidden_alerts.open || alarm_touch_capture) {
                bool tap=touch.down && !alarm_pressed;
                alarm_pressed=touch.down; alarm_touch_capture=touch.down;
                if(hidden_alerts.open && tap && DisplaySettings::inside(touch.x,touch.y,327,445,697,489)) {
                    hidden_alerts.acknowledge(); ESP_LOGI("hidden","ACKNOWLEDGED");
                }
                if(!touch.down) settings.pressed=false;
            } else changes|=settings.touch(touch);
        }
        if(changes&DisplaySettings::Layout) {
            rebuild_composition(); composition_revision++;
            for(unsigned i=0;i<5;i++) ESP_LOGI("layout","slot %u = %s",i,gauge_face_specs[static_cast<unsigned>(settings.gauges[i])].name);
        }
        if(changes&DisplaySettings::Backlight) set_backlight();
        if(changes&DisplaySettings::Save) save_settings();
        background=settings.night?night_background:day_background;
        frame=next?buffers1:buffers0;
        unsigned index=next?1:0;
        EngineData data=demo(esp_timer_get_time()/1000000.0f);
        if(data.rpm==0 && data.trim>60) {
            if(!trim_interlock_logged) ESP_LOGI("alarm","TRIM ENGINE_STOPPED value=%.1f rpm=0",data.trim);
            trim_interlock_logged=true;
        } else trim_interlock_logged=false;
        const float values[]={data.rpm,data.temperature,data.oil,data.fuel,data.voltage,data.trim,data.afr,data.intake_air,data.accelerator,data.fuel_pressure};
        const char* names[]={"RPM","TEMP","OIL","FUEL","VOLTAGE","TRIM","AFR","INTAKE","ACCELERATOR","FUEL_PRESSURE"};
        Alert current_alerts[gauge_count];
        for(unsigned i=0;i<gauge_count;i++) {
            Alert alert=gauge_alert(static_cast<Gauge>(i),values[i],data.rpm);
            current_alerts[i]=alert;
            if(alert!=previous_alerts[i]) {
                ESP_LOGI("alarm","%s %s value=%.1f",names[i],alert==Alert::Critical?"CRITICAL":(alert==Alert::Warning?"WARNING":"CLEAR"),values[i]);
                previous_alerts[i]=alert;
            }
        }
        bool was_open=hidden_alerts.open;
        if(hidden_alerts.update(settings.gauges,current_alerts))
            ESP_LOGI("hidden","%s mask=0x%03x critical=0x%03x",hidden_alerts.open?"OPEN":"CLOSED",hidden_alerts.mask,hidden_alerts.critical_mask);
        if(!was_open && hidden_alerts.open) {
            alarm_pressed=settings.pressed || alarm_touch_capture;
            if(settings.dragging && settings.dirty) save_settings();
            settings.dragging=false; settings.dirty=false;
        }
        unsigned overlay=hidden_alerts.open?3:(settings.open?1:(settings.chooser_slot>=0?2:0));
        if(previous_overlay[index]!=overlay || previous_night[index]!=settings.night || previous_composition[index]!=composition_revision)
            memcpy(frame,background,1024*600*2);
        previous_overlay[index]=overlay; previous_night[index]=settings.night; previous_composition[index]=composition_revision;
        render(data);
        if(hidden_alerts.open) hidden_alert_popup(values);
        else if(settings.open) settings_popup();
        else if(settings.chooser_slot>=0) gauge_picker();
        xSemaphoreTake(vsync,0);
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel,0,0,1024,600,frame));
        xSemaphoreTake(vsync,pdMS_TO_TICKS(100));
        if(n==0) wavesahre_rgb_lcd_bl_on();
        next=!next;
        if(n%10==0) { printf("DEMO running: frame=%u, uptime_ms=%lld, free heap=%u\n",n,esp_timer_get_time()/1000,unsigned(esp_get_free_heap_size())); fflush(stdout); }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
