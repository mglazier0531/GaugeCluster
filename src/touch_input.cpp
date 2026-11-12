#include "touch_input.h"
#include "io_extension.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"

static i2c_master_dev_handle_t controller;
static QueueHandle_t samples;
static int width=1024,height=600;
static esp_err_t read_reg(uint16_t reg,uint8_t* data,size_t size) {
    uint8_t address[]={uint8_t(reg>>8),uint8_t(reg)};
    return i2c_master_transmit_receive(controller,address,2,data,size,30);
}
static void poll(void*) {
    TouchSample last={false,0,0};
    int64_t last_report=esp_timer_get_time();
    for(;;) {
        uint8_t status=0;
        TouchSample next=last;
        bool report=false;
        if(read_reg(0x814e,&status,1)==ESP_OK && (status&0x80)) {
            unsigned count=status&15;
            if(count>=1 && count<=5) {
                uint8_t point[8];
                if(read_reg(0x814f,point,sizeof(point))==ESP_OK) {
                    int x=point[1]|(point[2]<<8),y=point[3]|(point[4]<<8);
                    if(x<width && y<height) { next={true,x*1024/width,y*600/height}; report=true; }
                }
            } else if(count==0) { next.down=false; report=true; }
            uint8_t clear[]={0x81,0x4e,0};
            i2c_master_transmit(controller,clear,sizeof(clear),30);
            last_report=esp_timer_get_time();
        }
        // Release capture on a disconnected controller rather than retaining a drag.
        if(last.down && esp_timer_get_time()-last_report>500000) { next.down=false; report=true; }
        if(report && (next.down!=last.down || (next.down && (next.x!=last.x || next.y!=last.y)))) {
            if(xQueueSend(samples,&next,0)==pdTRUE) last=next;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
bool touch_input_init(i2c_master_bus_handle_t bus) {
    // Waveshare GT911 reset/address-select sequence: EXIO1 reset, GPIO4 INT low.
    gpio_set_direction(GPIO_NUM_4,GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_4,0);
    IO_EXTENSION_Output(IO_EXTENSION_IO_1,0);
    vTaskDelay(pdMS_TO_TICKS(100));
    IO_EXTENSION_Output(IO_EXTENSION_IO_1,1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_direction(GPIO_NUM_4,GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(50));
    int address=0;
    const int addresses[]={0x5d,0x14};
    for(int candidate:addresses) {
        if(i2c_master_probe(bus,candidate,50)==ESP_OK) { address=candidate; break; }
    }
    if(!address) { ESP_LOGE("touch","GT911 not found; dashboard continues without touch"); return false; }
    i2c_device_config_t config={};
    config.device_address=address; config.scl_speed_hz=400000;
    if(i2c_master_bus_add_device(bus,&config,&controller)!=ESP_OK) return false;
    uint8_t id[4]={},bounds[4]={};
    if(read_reg(0x8140,id,4)!=ESP_OK) return false;
    if(read_reg(0x8048,bounds,4)==ESP_OK) {
        int w=bounds[0]|(bounds[1]<<8),h=bounds[2]|(bounds[3]<<8);
        if(w>0 && h>0) { width=w; height=h; }
    }
    ESP_LOGI("touch","GT911 %.4s at 0x%02x, coordinates %dx%d",id,address,width,height);
    samples=xQueueCreate(32,sizeof(TouchSample));
    if(!samples) return false;
    return xTaskCreate(poll,"touch_input",3072,nullptr,3,nullptr)==pdPASS;
}
bool touch_input_next(TouchSample& sample) { return samples && xQueueReceive(samples,&sample,0)==pdTRUE; }
