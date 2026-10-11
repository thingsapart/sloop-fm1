#include "runtime.h"
#include "core.h"
#include "board_services.h"
#include "storage_files.h"
#include "esp_heap_caps.h"
#include "freertos/task.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <unistd.h>
namespace host {
static uint8_t* copy;
static std::atomic<int> state{0}; // 0 idle, 1 request, 2 writing
static std::atomic<const char*> notice{"SD unavailable"};
static bool mounted;
static ImageFiles files("/sdcard/sloop-p4-a.bin","/sdcard/sloop-p4-b.bin","/sdcard/sloop-p4.tmp");
const char* storage_status(){return notice.load();}
const uint8_t* storage_load(){return mounted && files.load(copy)?copy:nullptr;}
void storage_request(){if(!mounted)return;int idle=0;if(state.compare_exchange_strong(idle,1))notice="Save requested";}
void storage_service(){
    if(state!=1)return;
    if(!sloop_quiet()){notice="Stop/release notes to save";return;}
    // Audio remains owned here; checkpoint/copy cost is explicitly part of telemetry.
    sloop_checkpoint();std::memcpy(copy,sloop_storage(),SLOOP_STORAGE_SIZE);state=2;
}
void storage_start(){
    // Deliberately avoid mount_storage(): whole-image replacement cannot safely fit
    // in the board's 2 MiB internal filesystem, and must not format it.
    mounted=p4::mount_sd();copy=static_cast<uint8_t*>(heap_caps_malloc(SLOOP_STORAGE_SIZE,MALLOC_CAP_SPIRAM));if(!copy)abort();
    if(mounted)notice="SD ready";
    if(xTaskCreatePinnedToCore([](void*){for(;;){if(state==2){
        bool ok=files.commit(copy);notice=ok?"Saved to SD":"SD save failed";state=0;
    }vTaskDelay(pdMS_TO_TICKS(20));}},"sloop-store",4096,nullptr,2,nullptr,0)!=pdPASS)abort();
}
}
