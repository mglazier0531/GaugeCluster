#include "rgb_lcd_port.h"
#include "dashboard_assets.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/semphr.h"
#include <cmath>
#include <cstdio>
#include <cstring>

// Rendering is independent of the source of engine data. NMEA 2000 comes next.
struct EngineData { float rpm, temperature, oil, fuel, voltage, speed, hours, flow; };
static uint16_t *frame;
static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t vsync;
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
        if(right>left) memcpy(frame+y*1024+left,dashboard_background+y*1024+left,(right-left)*2);
    }
}
static void text(const char* str,int cx,int top,int height,int maxwidth,bool center_vertical=false) {
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
            unsigned r=(((bg>>11)&31)*(255-a)+31*a)/255;
            unsigned g=(((bg>>5)&63)*(255-a)+63*a)/255;
            unsigned b=((bg&31)*(255-a)+31*a)/255;
            pixel(dx,dy,(r<<11)|(g<<5)|b);
        }
        left+=int(font_widths[index]*scale);
    }
}
static void band(unsigned gauge,float fraction) {
    unsigned cutoff=unsigned(lroundf(65535*fminf(1,fmaxf(0,fraction))));
    for(unsigned i=band_ranges[gauge];i<band_ranges[gauge+1];i++) {
        const auto& p=band_pixels[i];
        frame[p.offset]=(cutoff>0 && p.threshold<=cutoff)?p.active:p.inactive;
    }
}
static EngineData demo(float t) {
    float wave=0.5f-0.5f*cosf(t*0.28f);
    return {800+4400*wave,176+6*sinf(t*.12f),30+35*wave,
            72+2*sinf(t*.04f),13.8f+.25f*sinf(t*.3f),
            3+42*wave,487.6f+t/3600,1.2f+18*wave};
}
static void render(const EngineData& d) {
    // Only restore changing regions; full-frame flash-to-PSRAM copies compete with LCD DMA.
    const int areas[][4]={{390,250,640,345},{135,186,240,238},{140,382,240,433},
        {780,168,910,222},{775,382,914,434},{676,21,848,76},
        {169,515,321,558},{474,515,635,558}};
    for(auto& a:areas) restore(a[0],a[1],a[2],a[3]);
    char s[40];
    int rpm=int(d.rpm);
    if(rpm<1000) snprintf(s,sizeof(s),"%d",rpm);
    else snprintf(s,sizeof(s),"%d,%03d",rpm/1000,rpm%1000);
    text(s,511,250,76,226);
    snprintf(s,sizeof(s),"%.0f",d.temperature); text(s,175,186,43,80); text("F",224,194,24,20);
    snprintf(s,sizeof(s),"%.0f",d.oil); text(s,171,382,43,70); text("PSI",216,397,21,40);
    snprintf(s,sizeof(s),"%.0f%%",d.fuel); text(s,841,168,45,114);
    snprintf(s,sizeof(s),"%.1f",d.voltage); text(s,829,382,43,99); text("V",889,399,24,24);
    snprintf(s,sizeof(s),"%.1f MPH",d.speed); text(s,760,48,42,160,true);
    snprintf(s,sizeof(s),"%.1f h",d.hours); text(s,243,515,35,142);
    snprintf(s,sizeof(s),"%.1f GPH",d.flow); text(s,554,515,35,150);
    band(0,d.rpm/7000);
    band(1,(d.temperature-100)/160);
    band(2,d.oil/100);
    band(3,d.fuel/100);
    band(4,(d.voltage-10)/6);
}
extern "C" void app_main() {
    printf("GaugeCluster: 1024x600 ANIMATED DEMO; NMEA2000 disabled\n");
    DEV_I2C_Init(); IO_EXTENSION_Init();
    IO_EXTENSION_Output(IO_EXTENSION_IO_2,0);
    IO_EXTENSION_Output(IO_EXTENSION_IO_6,1);
    panel=waveshare_esp32_s3_rgb_lcd_init();
    vsync=xSemaphoreCreateBinary();
    esp_lcd_rgb_panel_event_callbacks_t cb={}; cb.on_vsync=on_vsync;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel,&cb,nullptr));
    void *a,*b; waveshare_get_frame_buffer(&a,&b);
    auto *buffers0=static_cast<uint16_t*>(a); auto *buffers1=static_cast<uint16_t*>(b);
    memcpy(buffers0,dashboard_background,1024*600*2);
    memcpy(buffers1,dashboard_background,1024*600*2);
    bool next=false;
    for(unsigned n=0;;n++) {
        frame=next?buffers1:buffers0;
        render(demo(esp_timer_get_time()/1000000.0f));
        xSemaphoreTake(vsync,0);
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel,0,0,1024,600,frame));
        xSemaphoreTake(vsync,pdMS_TO_TICKS(100));
        if(n==0) wavesahre_rgb_lcd_bl_on();
        next=!next;
        if(n%10==0) { printf("DEMO running: frame=%u, uptime_ms=%lld, free heap=%u\n",n,esp_timer_get_time()/1000,unsigned(esp_get_free_heap_size())); fflush(stdout); }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
