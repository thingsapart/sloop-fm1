#pragma once
#include <cstdint>
#include <cstdlib>
namespace host {
// Pointer ownership and relative motion, independent of LVGL/hardware.
class Adjuster {
public:
    int id=-1,start_y=0,last_y=0;uint32_t context=0;bool moving=false;
    void begin(int target,uint32_t token,int y){id=target;context=token;start_y=last_y=y;moving=false;}
    void end(){id=-1;moving=false;}
    int move(int y,uint32_t token){
        if(id<0)return 0;if(token!=context){end();return 0;}
        if(!moving && std::abs(y-start_y)<6)return 0;
        moving=true;int delta=(last_y-y)/4;last_y-=delta*4;return delta;
    }
};
}
