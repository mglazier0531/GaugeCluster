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
static unsigned buffer_index, marker_index;
static int old_markers[2][4][2] = {};
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
    for(int y=y0;y<y1;y++) {
        if(y<0 || y>=600) continue;
        int left=x0<0?0:x0, right=x1>1024?1024:x1;
        if(right>left) memcpy(frame+y*1024+left,dashboard_background+y*1024+left,(right-left)*2);
    }
}
static void text(const char* str,int cx,int top,int height,int maxwidth) {
    float scale=height/100.0f;
    int sum=0;
    for(const char* p=str;*p;p++) { const char* q=strchr(font_chars,*p); if(q) sum+=font_widths[q-font_chars]; }
    if(sum*scale>maxwidth) scale=float(maxwidth)/sum;
    int left=cx-int(sum*scale)/2;
    const int stride=96*(sizeof(font_chars)-1);
    for(const char* p=str;*p;p++) {
        const char* q=strchr(font_chars,*p); if(!q) continue;
        int index=q-font_chars;
        for(int y=0;y<int(120*scale);y++) for(int x=0;x<int(96*scale);x++) {
            int sx=int(x/scale), sy=int(y/scale);
            unsigned a=font_alpha[sy*stride+index*96+sx];
            int dx=left+x,dy=top+y;
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
static void indicator(float cx,float cy,float radius,float fraction) {
    float angle=(145+250*fminf(1,fmaxf(0,fraction)))*3.14159265f/180;
    int x=int(cx+radius*cosf(angle)), y=int(cy+radius*sinf(angle));
    old_markers[buffer_index][marker_index][0]=x;
    old_markers[buffer_index][marker_index++][1]=y;
    for(int dy=-4;dy<=4;dy++) for(int dx=-4;dx<=4;dx++)
        if(dx*dx+dy*dy<=16) pixel(x+dx,y+dy,0x07ff);
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
    for(auto& p:old_markers[buffer_index]) restore(p[0]-5,p[1]-5,p[0]+6,p[1]+6);
    marker_index=0;
    char s[40];
    snprintf(s,sizeof(s),"%d,%03d",int(d.rpm)/1000,int(d.rpm)%1000); text(s,511,250,76,226);
    snprintf(s,sizeof(s),"%.0f",d.temperature); text(s,175,186,43,80); text("F",224,194,24,20);
    snprintf(s,sizeof(s),"%.0f",d.oil); text(s,171,382,43,70); text("PSI",216,397,21,40);
    snprintf(s,sizeof(s),"%.0f%%",d.fuel); text(s,841,168,45,114);
    snprintf(s,sizeof(s),"%.1f",d.voltage); text(s,829,382,43,99); text("V",889,399,24,24);
    snprintf(s,sizeof(s),"%.1f MPH",d.speed); text(s,760,21,42,160);
    snprintf(s,sizeof(s),"%.1f h",d.hours); text(s,243,515,35,142);
    snprintf(s,sizeof(s),"%.1f GPH",d.flow); text(s,554,515,35,150);
    indicator(506,290,157,d.rpm/7000);
    indicator(182,190,90,(d.temperature-100)/160);
    indicator(183,397,90,d.oil/100);
    indicator(840,397,90,(d.voltage-10)/6);
    text("DEMO",512,571,14,80);
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
        buffer_index=next?1:0;
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
