#include "runtime.h"
#include "core.h"
#include "board_services.h"
#include "audio_device.h"
#include "audio_preferences.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/task.h"
#include <atomic>
#include <algorithm>
#include <cstring>
namespace host {
QueueHandle_t commands;
std::atomic<uint32_t> key_lights[27];
static std::atomic<bool> recover{false};
bool event(int kind,int id,int value,uint32_t context){
    Command c{kind,id,value,context};bool ok=xQueueSend(commands,&c,0)==pdTRUE;
    if(!ok)recover=true;return ok;
}
void panic(){recover=true;}
static void run(void*) {
    sloop_init(storage_load());sloop_event(SLOOP_MASTER,0,4096); // device master controls listening level
    int16_t output[256];
    auto* rgb=static_cast<uint16_t*>(heap_caps_malloc(240*240*2,MALLOC_CAP_SPIRAM));
    if(!rgb)abort();uint32_t lights[16]{};
    static HelpState help;uint32_t lastHelp=~0u;
    bool held[2][41]{};unsigned volume=unsigned(p4::preferred_master_volume()*100);
    int64_t lastUI=0,lastReport=0;unsigned peak_us=0,overruns=0;
    uint32_t lastFailures=0,lastTimeouts=0,lastErrors=0;
    for(;;){
        int64_t start=esp_timer_get_time();
        if(recover.exchange(false)){
            xQueueReset(commands);std::memset(held,0,sizeof held);sloop_event(SLOOP_PANIC,0,0);
        }
        Command c;for(unsigned n=0;n<128 && xQueueReceive(commands,&c,0)==pdTRUE;++n){
            int source=(c.kind>>8)&1,kind=c.kind&255;
            if(kind==9){sloop_encoder_guarded(c.id,c.value,c.context);continue;}
            if(kind==8){volume=std::clamp(int(volume)+c.value,0,100);p4_audio->set_volume(volume);continue;}
            if(kind==0 || kind==2){
                int index=kind==0?c.id:14+c.id;
                if(index<0 || index>=41)continue;
                bool before=held[0][index]||held[1][index];held[source][index]=c.value!=0;
                bool after=held[0][index]||held[1][index];if(before==after)continue;c.value=after;
            }
            if(kind==SLOOP_PANIC)std::memset(held,0,sizeof held);
            sloop_event(kind,c.id,c.value);
        }
        sloop_render_i16(output,128);
        int64_t now=esp_timer_get_time();
        if(now-lastUI>=40000){sloop_tick(uint32_t(now/1000));sloop_pixels565(rgb);sloop_lights(lights);
            uint32_t keys[27];sloop_key_lights(keys);for(unsigned i=0;i<27;++i)key_lights[i]=keys[i];
            help.fn=fn_active();help.pressed=physical_keys[0].load()|physical_keys[1].load();
            sloop_help_context(help.context,sizeof help.context);
            sloop_controls_snapshot(help.controls);help.layer=sloop_active_layer();help.recording=sloop_recording();
            std::snprintf(help.controls[7].value,sizeof help.controls[7].value,"%u%%",volume);
            help.locked_key=-1;int locked=sloop_locked_button();
            for(unsigned i=0;i<30;++i){auto b=physical_binding(i,false);if(b.kind==HelpKind::Button && int(b.id)==locked)help.locked_key=i;}
            locked_physical=help.locked_key;
            for(unsigned i=0;i<30;++i){auto b=physical_binding(i,help.fn);help.keys[i]=help_card(b);
                if(b.kind==HelpKind::Note)sloop_key_hint(b.id,help.keys[i].title,sizeof help.keys[i].title,help.keys[i].summary,sizeof help.keys[i].summary);}
            uint32_t selected=help_event.load();
            if(selected!=lastHelp){Binding b{HelpKind(selected&255),(selected>>8)&255};help.current=help_card(b);
                if(b.kind==HelpKind::Note)sloop_key_hint(b.id,help.current.title,sizeof help.current.title,help.current.summary,sizeof help.current.summary);
                lastHelp=selected;
            }
            surface_publish(rgb,lights,sloop_playing(),help);lastUI=now;}
        storage_service();
        unsigned elapsed=unsigned(esp_timer_get_time()-start);peak_us=std::max(peak_us,elapsed);
        if(elapsed>2902)++overruns;
        if(now-lastReport>=1000000){
            auto d=p4_audio->diagnostics();
            if(overruns || d.failed_writes!=lastFailures || d.wait_timeouts!=lastTimeouts || d.dma_errors!=lastErrors)
                ESP_LOGW("SLOOP","Service peak %u us, over-period %u; audio failures %lu, timeouts %lu, DMA errors %lu",
                    peak_us,overruns,(unsigned long)d.failed_writes,(unsigned long)d.wait_timeouts,(unsigned long)d.dma_errors);
            lastFailures=d.failed_writes;lastTimeouts=d.wait_timeouts;lastErrors=d.dma_errors;
            peak_us=overruns=0;lastReport=now;
        }
        // submit() provides bounded queue backpressure. Waiting for a consumed
        // token first leaves the queue empty during the original UI's redraws.
        // Keep the driver's two queued blocks and four DMA blocks primed instead.
        p4_audio->submit(output,128);
    }
}
}
extern "C" void app_main(){
    ESP_ERROR_CHECK(nvs_flash_init()); // Never erase another application's preferences on a mount error.
    ESP_ERROR_CHECK(p4::hardware_init());p4::load_audio_preferences();
    host::commands=xQueueCreate(256,sizeof(host::Command));if(!host::commands)abort();
    p4_audio=new P4Audio;ESP_ERROR_CHECK(p4_audio->init());
    p4_audio->set_volume(unsigned(p4::preferred_master_volume()*100));
    p4_audio->set_route(P4Audio::Route(p4::preferred_output()));
    host::storage_start();host::surface_start();host::keyboard_start();
    if(xTaskCreatePinnedToCore(host::run,"sloop",16384,nullptr,16,nullptr,1)!=pdPASS)abort();
}
