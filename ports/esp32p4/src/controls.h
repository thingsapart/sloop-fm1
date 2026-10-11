#pragma once
#include <atomic>
#include <algorithm>
#include <cstdint>
#include "help.h"
namespace host {
inline std::atomic<int> wheel_target{0}; // SELECT, ALGO, PRESETS, K1..K4, MASTER
class Controls {
public:
    using Sink=void(*)(int,int,int);
    explicit Controls(Sink sink,unsigned source=0):sink_(sink),source_(source) {}
    bool function_held()const{return fn_active();}
    void key(unsigned id,bool down) {
        if(id>=30)return;
        if(id==29){
            if(bool(physical_keys[source_].load()&(1u<<29))==down)return;
            if(down)fn_sources.fetch_or(1u<<source_);else fn_sources.fetch_and(~(1u<<source_));
            if(down){physical_keys[source_].fetch_or(1u<<id);show_help(HelpKind::Fn);}
            else physical_keys[source_].fetch_and(~(1u<<id));return;
        }
        if(down) {
            if(held_[id])return;held_[id]=true;physical_keys[source_].fetch_or(1u<<id);
            Binding b=physical_binding(id,function_held());show_help(b.kind,b.id);
            switch(b.kind){
            case HelpKind::Wheel:wheel_target=b.id;break;
            case HelpKind::OctChord:buttons_[id]=(1u<<12)|(1u<<13);buttons(id,1);break;
            case HelpKind::Note:notes_[id]=b.id+1;sink_(2,b.id,1);break;
            case HelpKind::Button:buttons_[id]=1u<<b.id;buttons(id,1);break;
            default:break;
            }
        } else {
            if(notes_[id])sink_(2,notes_[id]-1,0);
            buttons(id,0);notes_[id]=buttons_[id]=0;held_[id]=false;
            physical_keys[source_].fetch_and(~(1u<<id));
        }
    }
    void turn(int delta) {
        int target=wheel_target.load();if(delta)show_help(HelpKind::Wheel,target);
        if(target==7){sink_(8,0,delta);return;}
        while(delta){int n=std::clamp(delta,-32,32);sink_(1,target,n);delta-=n;}
    }
    void release_all(){for(unsigned i=0;i<30;++i)key(i,false);}
    void reset(){release_all();sink_(4,0,0);show_help(HelpKind::Disconnected);}
private:
    Sink sink_;unsigned source_;bool held_[30]{};uint32_t buttons_[30]{};int notes_[30]{};unsigned refs_[14]{};
    void buttons(unsigned id,int value){for(unsigned b=0;b<14;++b)if(buttons_[id]&(1u<<b)){
        if(value){if(refs_[b]++==0)sink_(0,b,1);}
        else if(refs_[b] && --refs_[b]==0)sink_(0,b,0);
    }}
};
}
