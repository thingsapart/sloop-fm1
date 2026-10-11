#pragma once
#include <cstdint>
#include <initializer_list>
namespace host {
// sRGB swatches arranged against horizontal AND vertical neighbours in the
// physical 5x6 matrix. See colors_test.cpp for D65 CIELAB separation checks.
inline constexpr uint32_t key_palette[]={0x2468ff,0xffd000,0xed30b0,0x00d9c0,0xff5a20,0x86f000};
inline constexpr uint8_t key_color_index[]={
    0,1,2,3,4,5,
    5,0,5,2,3,2,
    0,1,0,5,4,3,
    1,0,5,2,3,4,
    0,1,0,5,2,3
};
inline uint32_t key_color(unsigned id){return id<30?key_palette[key_color_index[id]]:0;}
inline uint32_t key_text_color(unsigned id){return id<30 && key_color_index[id]==0?0xffffff:0x000000;}
inline uint32_t key_color_level(unsigned id,bool active){
    uint32_t rgb=key_color(id),result=0;unsigned level=active?255:90;
    for(unsigned shift:{0u,8u,16u})result|=(((rgb>>shift)&255)*level/255)<<shift;
    return result;
}
}
