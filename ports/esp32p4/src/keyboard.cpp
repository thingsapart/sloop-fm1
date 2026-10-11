#include "runtime.h"
#include "controls.h"
#include "key_colors.h"
#include "esp32_keyboard.h"
#include "board_services.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/task.h"
namespace host {
static void send(int kind,int id,int value){event(kind|0x100,id,value);}
static Controls controls(send);
static bool receive(const klickwerk_keyboard::Event& e,void*) {
    if(e.kind==KW_KEY)controls.key(e.id,e.value!=0);
    else if(e.kind==KW_ENCODER)controls.turn(e.value);
    return true; // Runtime overflow invokes panic; never replay accepted note-down.
}
static bool reset(const klickwerk_keyboard::Status&,void*){controls.reset();return true;}
void keyboard_start(){
    if(xTaskCreatePinnedToCore([](void*){
        klickwerk_keyboard::Keyboard keyboard;ESP_ERROR_CHECK(keyboard.begin(p4::i2c_bus));
        klickwerk_keyboard::Sink sink{receive,reset,nullptr};
        int64_t good=esp_timer_get_time(),led_at=0;bool lost=false;uint16_t generation=0;
        for(;;){auto err=keyboard.poll(sink);
            if(err==ESP_OK){good=esp_timer_get_time();lost=false;
                if(good-led_at>50000){
                    uint8_t pixels[KW_PIXELS*4]{};
                    uint32_t pressed=physical_keys[0].load()|physical_keys[1].load();
                    for(unsigned i=0;i<KW_PIXELS;++i){uint32_t rgb=0;
                        if(i<30){auto binding=physical_binding(i,fn_active());bool active=(pressed&(1u<<i))!=0;
                            if(binding.kind==HelpKind::Note)active|=key_lights[binding.id].load()==0x60c080;
                            if(binding.kind==HelpKind::Wheel)active|=wheel_target==int(binding.id);
                            active|=locked_physical==int(i);
                            if(binding.kind!=HelpKind::Unused)rgb=key_color_level(i,active);
                        }
                        if(i==30)rgb=keyboard.status().active_route==KW_ACTIVE_I2C?0x005020:0x002050;
                        pixels[i*4]=rgb>>16;pixels[i*4+1]=rgb>>8;pixels[i*4+2]=rgb;pixels[i*4+3]=255;
                    }
                    if(keyboard.colors(0,KW_PIXELS,pixels)==ESP_OK)keyboard.commit(++generation,32,true);
                    led_at=good;
                }
            }
            else if(!lost && esp_timer_get_time()-good>250000){controls.reset();keyboard.disconnected();lost=true;}
            vTaskDelay(pdMS_TO_TICKS(lost?100:2));
        }
    },"psion",4096,nullptr,4,nullptr,0)!=pdPASS)abort();
}
}
