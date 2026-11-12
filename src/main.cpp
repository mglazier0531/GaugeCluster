#include "rgb_lcd_port.h"
#include "dashboard_assets.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/semphr.h"
#include "touch_input.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "gauge_limits.h"
#include <cmath>
#include <cstdio>
#include <cstring>

// Rendering is independent of the source of engine data. NMEA 2000 comes next.
struct EngineData { float rpm, temperature, oil, fuel, voltage, speed, hours, flow; };
static uint16_t *frame;
static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t vsync;
static DisplaySettings settings;
static uint16_t *night_background,*night_palette;
static const uint16_t* background=dashboard_background;
static uint16_t themed(uint16_t color) { return settings.night?night_palette[color]:color; }
static void prepare_night_palette() {
    night_palette=static_cast<uint16_t*>(heap_caps_malloc(65536*2,MALLOC_CAP_SPIRAM));
    night_background=static_cast<uint16_t*>(heap_caps_malloc(1024*600*2,MALLOC_CAP_SPIRAM));
    assert(night_palette && night_background);
    for(unsigned c=0;c<65536;c++) {
        unsigned r=((c>>11)&31)*255/31,g=((c>>5)&63)*255/63,b=(c&31)*255/31;
        unsigned l=(r*30+g*59+b*11)/100;
        unsigned nr=l*3/4,ng=l/5,nb=l/32;
        if(r>g*2 && r>b*2) { nr=r*3/4; ng=g/6; nb=0; }
        night_palette[c]=((nr>>3)<<11)|((ng>>2)<<5)|(nb>>3);
    }
    for(unsigned i=0;i<1024*600;i++) night_background[i]=night_palette[dashboard_background[i]];
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
static void rounded_rect(int l,int t,int r,int b,int radius,uint16_t color) {
    color=themed(color);
    for(int y=t;y<b;y++) for(int x=l;x<r;x++) {
        int dx=x<l+radius?l+radius-x:(x>=r-radius?x-(r-radius-1):0);
        int dy=y<t+radius?t+radius-y:(y>=b-radius?y-(b-radius-1):0);
        if(dx*dx+dy*dy<=radius*radius) pixel(x,y,color);
    }
}
static void ui_text(const char* value,int cx,int center_y,int height,int maxwidth) {
    text(value,cx,int(center_y/dashboard_y_scale),height,maxwidth,true);
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
static EngineData demo(float t) {
    float wave=0.5f-0.5f*cosf(t*0.28f);
    EngineData data={800+4200*wave,176+6*sinf(t*.12f),30+35*wave,
            72+2*sinf(t*.04f),13.8f+.25f*sinf(t*.3f),
            3+42*wave,487.6f+t/3600,1.2f+18*wave};
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
    return data;
}
static void render(const EngineData& d) {
    bool flash_on=(esp_timer_get_time()/500000)%2==0;
    Alert alerts[]={gauge_alert(Gauge::Rpm,d.rpm),gauge_alert(Gauge::Temperature,d.temperature),
        gauge_alert(Gauge::Oil,d.oil),gauge_alert(Gauge::Fuel,d.fuel),gauge_alert(Gauge::Voltage,d.voltage)};
    uint16_t colors[5];
    for(unsigned i=0;i<5;i++) colors[i]=alert_ink(alerts[i],flash_on);
    // Only restore changing regions; full-frame flash-to-PSRAM copies compete with LCD DMA.
    const int areas[][4]={{390,250,640,345},{135,186,240,238},{140,382,240,433},
        {780,168,910,222},{775,382,914,434},{676,21,848,76},
        {169,515,321,558},{474,515,635,558}};
    for(auto& a:areas) restore(a[0],a[1],a[2],a[3]);
    char s[40];
    int rpm=int(d.rpm);
    if(rpm<1000) snprintf(s,sizeof(s),"%d",rpm);
    else snprintf(s,sizeof(s),"%d,%03d",rpm/1000,rpm%1000);
    text(s,511,250,76,226,false,colors[0]);
    snprintf(s,sizeof(s),"%.0f",d.temperature); text(s,175,186,43,80,false,colors[1]); text("F",224,194,24,20,false,colors[1]);
    snprintf(s,sizeof(s),"%.0f",d.oil); text(s,171,382,43,70,false,colors[2]); text("PSI",216,397,21,40,false,colors[2]);
    snprintf(s,sizeof(s),"%.0f%%",d.fuel); text(s,841,168,45,114,false,colors[3]);
    snprintf(s,sizeof(s),"%.1f",d.voltage); text(s,829,382,43,99,false,colors[4]); text("V",889,399,24,24,false,colors[4]);
    snprintf(s,sizeof(s),"%.1f MPH",d.speed); text(s,760,48,42,160,true);
    snprintf(s,sizeof(s),"%.1f h",d.hours); text(s,243,515,35,142);
    snprintf(s,sizeof(s),"%.1f GPH",d.flow); text(s,554,515,35,150);
    band(0,d.rpm/rpm_max,alerts[0],flash_on);
    band(1,(d.temperature-100)/160,alerts[1],flash_on);
    band(2,d.oil/100,alerts[2],flash_on);
    band(3,d.fuel/100,alerts[3],flash_on);
    band(4,(d.voltage-10)/6,alerts[4],flash_on);
}
extern "C" void app_main() {
    printf("GaugeCluster: 1024x600 ANIMATED DEMO; NMEA2000 disabled\n");
    load_settings(); prepare_night_palette();
    background=settings.night?night_background:dashboard_background;
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
    bool previous_open[2]={false,false},previous_night[2]={settings.night,settings.night};
    bool next=false;
    Alert previous_alerts[5]={};
    for(unsigned n=0;;n++) {
        TouchSample touch;
        unsigned changes=0;
        while(touch_input_next(touch)) changes|=settings.touch(touch);
        if(changes&DisplaySettings::Backlight) set_backlight();
        if(changes&DisplaySettings::Save) save_settings();
        background=settings.night?night_background:dashboard_background;
        frame=next?buffers1:buffers0;
        unsigned index=next?1:0;
        if((previous_open[index] && !settings.open) || previous_night[index]!=settings.night)
            memcpy(frame,background,1024*600*2);
        previous_open[index]=settings.open; previous_night[index]=settings.night;
        EngineData data=demo(esp_timer_get_time()/1000000.0f);
        const float values[]={data.rpm,data.temperature,data.oil,data.fuel,data.voltage};
        const char* names[]={"RPM","TEMP","OIL","FUEL","VOLTAGE"};
        for(unsigned i=0;i<5;i++) {
            Alert alert=gauge_alert(static_cast<Gauge>(i),values[i]);
            if(alert!=previous_alerts[i]) {
                ESP_LOGI("alarm","%s %s value=%.1f",names[i],alert==Alert::Critical?"CRITICAL":(alert==Alert::Warning?"WARNING":"CLEAR"),values[i]);
                previous_alerts[i]=alert;
            }
        }
        render(data);
        if(settings.open) settings_popup();
        xSemaphoreTake(vsync,0);
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel,0,0,1024,600,frame));
        xSemaphoreTake(vsync,pdMS_TO_TICKS(100));
        if(n==0) wavesahre_rgb_lcd_bl_on();
        next=!next;
        if(n%10==0) { printf("DEMO running: frame=%u, uptime_ms=%lld, free heap=%u\n",n,esp_timer_get_time()/1000,unsigned(esp_get_free_heap_size())); fflush(stdout); }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
