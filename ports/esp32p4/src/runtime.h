#pragma once
#include <cstdint>
#include <atomic>
#include "help.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
namespace host {
struct Command {int kind,id,value;uint32_t context;};
extern QueueHandle_t commands;
extern std::atomic<uint32_t> key_lights[27];
bool event(int kind,int id,int value,uint32_t context=0);
void panic();
void keyboard_start();
void surface_start();
void surface_publish(const uint16_t* rgb,const uint32_t* lights,bool playing,const HelpState& help);
void storage_start();
const uint8_t* storage_load();
void storage_request();
void storage_service(); // SLOOP owner; stopped-only checkpoint transfer
const char* storage_status();
}
