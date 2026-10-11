#pragma once
#include <atomic>
#include <cstdint>
#include <cstdio>
#include "../engine/core.h"
namespace host {
enum class HelpKind : unsigned { Idle,Button,Note,Wheel,Fn,OctChord,Unused,SD,Route,Disconnected };
struct Binding { HelpKind kind;unsigned id; };
struct HelpCard { char title[32]{};char summary[128]{}; };
struct HelpState { HelpCard keys[30],current;sloop_control_info controls[8]{};char context[64]{};
    uint32_t pressed=0;int locked_key=-1;unsigned layer=0;int recording=0;bool fn=false; };
inline std::atomic<int> locked_physical{-1};
inline std::atomic<uint32_t> help_event{0},fn_sources{0},physical_keys[2]{};
inline constexpr int white_keys[]={0,2,4,6,7,9,11,12,14,16,18,19,21,23,24,26};
inline constexpr int black_keys[]={1,3,5,8,10,13,15,17,20,22,25};
inline bool fn_active(){return fn_sources.load()!=0;}
inline void show_help(HelpKind kind,unsigned id=0){
    uint32_t old=help_event.load(),next;
    do{next=((old+0x10000u)&0xffff0000u)|(id<<8)|unsigned(kind);}
    while(!help_event.compare_exchange_weak(old,next));
}
inline Binding physical_binding(unsigned id,bool fn){
    if(id>=30)return {HelpKind::Unused,0};
    if(id==29)return {HelpKind::Fn,0};
    if(fn && id<6){constexpr int targets[]={3,4,5,6,0,2};return {HelpKind::Wheel,unsigned(targets[id])};}
    if(fn && (id==10 || id==16))return {HelpKind::Wheel,id==10?1u:7u};
    if(fn && id==23)return {HelpKind::OctChord,0};
    if(id>=6 && id%6<4){unsigned pad=(id/6-1)*4+id%6;
        if(fn && pad>=11)return {HelpKind::Unused,0};
        return {HelpKind::Note,unsigned(fn?black_keys[pad]:white_keys[pad])};}
    if(id<6)return {HelpKind::Button,id};
    switch(id){case 10:return {HelpKind::Button,6};case 16:return {HelpKind::Button,7};
        case 22:return {HelpKind::Button,8};case 23:return {HelpKind::Button,9};
        case 11:return {HelpKind::Button,10};case 17:return {HelpKind::Button,11};
        case 28:return {HelpKind::Button,fn?13u:12u};default:return {HelpKind::Unused,0};}
}
inline bool layer_key(unsigned id){return id==0 || id==1 || id==4 || id==5 || id==16 || id==22 || id==23;}
inline HelpCard help_card(Binding b){
    HelpCard c;const char* title="KEY HELP";const char* summary="Press a key to see its musical function.\nHELP opens the keyboard map.";
    static const char* names[]={"FX","SEL / KEY","ENV","LFO","EDIT","GLO / MIX","HOME","SAVE / SONG","ARP","SEQ","PLAY","REC","OCT-","OCT+"};
    static const char* descriptions[]={
        "Tap: effects pages.\nHold + pad: punch-in effects.",
        "Tap: scale/chord pages.\nHold + pad: set the song's key.",
        "Shape how a note starts and fades.\nK1-K4 adjust the current page.",
        "Add repeating movement to sound.\nK1-K4 adjust the current page.",
        "Tap: editing pages.\nHold + pad: erase notes or sounds.",
        "Tap: mix pages.\nHold + pads: mute, solo, fill, tempo.",
        "Tracks / visualiser. Hold: menu.\nTap with a held layer: lock it.",
        "Tap: save/song pages. Hold + pads:\nsections. SD SAVE writes to the card.",
        "Tap: arpeggiator pages.\nHold + pad: rhythmic note repeat.",
        "Tap: sequencer pages.\nHold + pad: add or edit a step.",
        "Start or stop all four tracks.",
        "Record / arm recording.\nHold ~2 seconds: clear selected track.",
        "Lower octave; drums: ghost hits.\nEDIT: undo. Held step: clear locks.",
        "Raise octave; drums: hard hits.\nEDIT: redo. Held step: fill condition."};
    static const char* wheels[]={"SELECT","ALGO","PRESETS","K1","K2","K3","K4","MASTER"};
    if(b.kind==HelpKind::Button && b.id<14){title=names[b.id];summary=descriptions[b.id];}
    else if(b.kind==HelpKind::Wheel && b.id<8){std::snprintf(c.title,sizeof c.title,"Wheel: %s",wheels[b.id]);
        summary=b.id==0?"Home / held layer: tempo.\nOn a page: browse its pages.":b.id==1?"Choose the track you play or edit.":b.id==2?"Choose a sound or drum kit.":b.id==7?"Adjust the listening volume.":"Adjust this knob's current parameter.\nIts musical role follows the page.";}
    else if(b.kind==HelpKind::Fn){title="Fn / SHIFT";summary="Extra keys + wheel shortcuts.\nOn-screen Fn: tap to latch/unlatch.";}
    else if(b.kind==HelpKind::OctChord){title="OCT- + OCT+";summary="Reset synth octave to its centre.\nDrum and editing contexts may differ.";}
    else if(b.kind==HelpKind::Unused){title="UNASSIGNED";summary="This Fn position has no action.\nRelease Fn for the normal pad.";}
    else if(b.kind==HelpKind::SD){title="SD SAVE";summary="Write all projects/settings to SD.\nWaits for stopped, quiet audio.";}
    else if(b.kind==HelpKind::Route){title="OUTPUT";summary="Cycle AUTO / onboard / USB audio.";}
    else if(b.kind==HelpKind::Disconnected){title="Keyboard disconnected";summary="Held keyboard input was released.";}
    else if(b.kind==HelpKind::Note){title="PAD";summary="Play the selected track.\nHeld layers change its musical role.";}
    if(!c.title[0])std::snprintf(c.title,sizeof c.title,"%s",title);
    std::snprintf(c.summary,sizeof c.summary,"%s",summary);return c;
}
}
