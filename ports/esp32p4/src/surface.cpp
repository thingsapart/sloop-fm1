#include "runtime.h"
#include "controls.h"
#include "key_colors.h"
#include "adjuster.h"
#include "panel.h"
#include "audio_device.h"
#include "esp_heap_caps.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
namespace host {
static SemaphoreHandle_t exchange;
static uint16_t *pending,*scaled;
static bool fresh=false,redraw=true,zoom=false,mapVisible=false;
static std::atomic<bool> playing{false};
static lv_obj_t *canvas,*status,*targets[8],*helpTitle,*helpSummary,*mapButton,*zoomButton;
static lv_obj_t *guide,*mapTitle,*mapDetail,*mapSummary,*keys[30]{};
static lv_obj_t *topBands[6];
static constexpr int screenSize=416,screenX=0,screenY=24;
static lv_obj_t *rail,*origin,*marker,*railValue;
static HelpState pendingHelp,shownHelp;
static unsigned mapMode=1; // 0 native, 1 automatic learning guide, 2 always guide
static Adjuster adjust;
static int contactX=0;static bool dragFn=false;
static void touch_send(int kind,int id,int value){event(kind,id,value);}
static Controls touchKeys(touch_send,1);
static void refresh_top_keys();
static void label_update(lv_obj_t* label,const char* text){if(std::strcmp(lv_label_get_text(label),text))lv_label_set_text(label,text);}
static void visible(lv_obj_t* obj,bool show){bool hidden=lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN);
    if(show && hidden)lv_obj_remove_flag(obj,LV_OBJ_FLAG_HIDDEN);else if(!show && !hidden)lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN);}
static void hide_rail(){for(auto* obj:{rail,origin,marker,railValue})visible(obj,false);}
static void stop_drag(){adjust.end();hide_rail();}
static void key(lv_event_t* e){
    unsigned id=unsigned(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));auto code=lv_event_get_code(e);
    if(code==LV_EVENT_PRESSED){
        if(id==29){bool held=(physical_keys[1].load()&(1u<<29))!=0;touchKeys.key(29,!held);}
        else touchKeys.key(id,true);
    }else if(id!=29 && (code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST))touchKeys.key(id,false);
    if(id<6 && (code==LV_EVENT_PRESSED || code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST))refresh_top_keys();
}
static void knob(lv_event_t* e){
    int id=int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));auto code=lv_event_get_code(e);
    if(code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST){stop_drag();return;}
    auto* input=lv_indev_active();if(!input)return;lv_point_t p{};lv_indev_get_point(input,&p);
    if(code==LV_EVENT_PRESSED){
        stop_drag();wheel_target=id;show_help(HelpKind::Wheel,id);
        if(!shownHelp.controls[id].enabled)return;
        adjust.begin(id,shownHelp.controls[id].context,p.y);contactX=p.x;dragFn=shownHelp.fn;
    }else if(code==LV_EVENT_PRESSING && adjust.id==id){
        if(dragFn!=shownHelp.fn || !shownHelp.controls[id].enabled){stop_drag();return;}
        int delta=adjust.move(p.y,shownHelp.controls[id].context);
        if(adjust.id<0){hide_rail();return;}
        if(delta){if(id==7)event(8,0,delta);else event(9,id,delta,adjust.context);}
        if(adjust.moving){
            int low=std::max(60,adjust.start_y-100),high=std::min(430,adjust.start_y+100);
            lv_obj_set_pos(rail,contactX-3,low);lv_obj_set_size(rail,6,high-low);
            lv_obj_set_pos(origin,contactX-10,adjust.start_y);lv_obj_set_pos(marker,contactX-7,std::clamp(int(p.y),60,430)-7);
            lv_obj_set_pos(railValue,contactX>704?contactX-124:std::min(contactX+18,684),std::clamp(int(p.y)-44,60,398));
            char text[96];std::snprintf(text,sizeof text,"%s\n%s",shownHelp.controls[id].label,shownHelp.controls[id].value);
            label_update(railValue,text);for(auto* obj:{rail,origin,marker,railValue})visible(obj,true);
        }
    }
}
void surface_publish(const uint16_t* rgb,const uint32_t*,bool running,const HelpState& help){
    if(xSemaphoreTake(exchange,0)!=pdTRUE)return;
    std::memcpy(pending,rgb,240*240*2);playing=running;pendingHelp=help;fresh=true;xSemaphoreGive(exchange);
}
static lv_obj_t* label(lv_obj_t* parent,int x,int y,int width){
    auto* obj=lv_label_create(parent);lv_obj_set_pos(obj,x,y);lv_obj_set_width(obj,width);
    lv_obj_set_style_text_font(obj,&lv_font_montserrat_14,0);lv_obj_set_style_text_color(obj,lv_color_hex(0xffffff),0);return obj;
}
static lv_obj_t* tile(const char* name,int x,int y,int w,int h,lv_obj_t* parent=nullptr){
    auto* obj=lv_button_create(parent?parent:lv_screen_active());lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,h);
    lv_obj_set_style_text_font(obj,&lv_font_montserrat_14,0);lv_obj_set_style_pad_all(obj,2,0);
    lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(obj,LV_OBJ_FLAG_PRESS_LOCK);
    auto* lab=lv_label_create(obj);lv_label_set_text(lab,name);lv_obj_set_width(lab,w-8);
    lv_obj_set_style_text_align(lab,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(lab);return obj;
}
static const char* baseNames[]={"FX","SEL","ENV","LFO","EDIT","GLO","HOME","SAVE","ARP","SEQ","PLAY","REC","OCT-","OCT+"};
static void create_key(unsigned id,int x,int y,int w,int h,lv_obj_t* parent=nullptr){
    keys[id]=tile("",x,y,w,h,parent);lv_obj_set_style_border_width(keys[id],3,0);
    lv_obj_set_style_border_color(keys[id],lv_color_hex(key_color(id)),0);
    lv_obj_add_event_cb(keys[id],key,LV_EVENT_ALL,reinterpret_cast<void*>(uintptr_t(id)));
}
static void refresh_top_keys(){
    static uint32_t previous=~0u;
    uint32_t pressed=(physical_keys[0].load()|physical_keys[1].load())&63u;
    if(pressed==previous)return;
    previous=pressed; // LVGL style/visibility setters emit events synchronously.
    for(unsigned id=0;id<6;++id){bool down=(pressed&(1u<<id))!=0;
        // The 88x44 parent remains the hit target even when only its 5px child is visible.
        lv_obj_set_style_bg_opa(keys[id],down?LV_OPA_COVER:LV_OPA_TRANSP,0);
        lv_obj_set_style_bg_opa(keys[id],down?LV_OPA_COVER:LV_OPA_TRANSP,LV_STATE_PRESSED);
        lv_obj_set_style_border_width(keys[id],down?3:0,0);
        lv_obj_set_style_bg_color(keys[id],lv_color_hex(key_color(id)),0);
        auto* lab=lv_obj_get_child(keys[id],0);visible(lab,down);visible(topBands[id],!down);
        lv_obj_set_style_text_color(lab,lv_color_hex(key_text_color(id)),0);
    }
}
static void update_keys(){
    static uint32_t previousPressed=~0u;
    for(unsigned id=0;id<30;++id){auto b=physical_binding(id,shownHelp.fn);char name[96];
        if(id>=6 && id%6<4)std::snprintf(name,sizeof name,"P%02u\n%s",(id/6-1)*4+id%6+1,shownHelp.keys[id].title);
        else if(b.kind==HelpKind::Wheel){static const char* wheel[]={"SELECT","ALGO","PRESETS","K1","K2","K3","K4","MASTER"};
            std::snprintf(name,sizeof name,"%s\n%s",wheel[b.id],shownHelp.controls[b.id].label);}
        else if(b.kind==HelpKind::Button){const char* title=baseNames[b.id];
            if((b.id==12 || b.id==13) && shownHelp.layer==2)title=b.id==12?"UNDO":"REDO";
            std::snprintf(name,sizeof name,"%s%s",title,shownHelp.locked_key==int(id)?"\nLOCK":"");}
        else std::snprintf(name,sizeof name,"%s",b.kind==HelpKind::Fn?"Fn":b.kind==HelpKind::OctChord?"OCT +/-":"--");
        label_update(lv_obj_get_child(keys[id],0),name);
        bool down=(shownHelp.pressed&(1u<<id))!=0;
        if(id>=6 && previousPressed!=shownHelp.pressed){
            lv_obj_set_style_bg_color(keys[id],lv_color_hex(down?key_color(id):0x182028),0);
            lv_obj_set_style_text_color(lv_obj_get_child(keys[id],0),lv_color_hex(down?key_text_color(id):0xffffff),0);
        }
    }
    previousPressed=shownHelp.pressed;
}
void surface_start(){
    exchange=xSemaphoreCreateMutex();pending=static_cast<uint16_t*>(heap_caps_calloc(240*240,2,MALLOC_CAP_SPIRAM));
    if(!exchange || !pending)abort();
    if(xTaskCreatePinnedToCore([](void*){
        lv_init();lv_display_t* display=nullptr;lv_indev_t* touch=nullptr;ESP_ERROR_CHECK(p4::panel_init(&display,&touch));
        lv_obj_set_style_bg_color(lv_screen_active(),lv_color_hex(0x101216),0);
        scaled=static_cast<uint16_t*>(heap_caps_calloc(480*480,2,MALLOC_CAP_SPIRAM));if(!scaled)abort();
        canvas=lv_canvas_create(lv_screen_active());lv_obj_set_pos(canvas,screenX,screenY);
        lv_canvas_set_buffer(canvas,scaled,screenSize,screenSize,LV_COLOR_FORMAT_RGB565);
        for(unsigned id=0;id<6;++id){int x=id<4?10+id*104:id==4?420:512;
            create_key(id,x,12,88,44);lv_obj_set_style_pad_all(keys[id],0,0);lv_obj_set_style_shadow_width(keys[id],0,0);
            topBands[id]=lv_obj_create(keys[id]);lv_obj_set_pos(topBands[id],0,0);lv_obj_set_size(topBands[id],88,5);
            lv_obj_set_style_bg_color(topBands[id],lv_color_hex(key_color(id)),0);lv_obj_set_style_bg_opa(topBands[id],LV_OPA_COVER,0);
            lv_obj_set_style_border_width(topBands[id],0,0);lv_obj_set_style_radius(topBands[id],0,0);
            lv_obj_remove_flag(topBands[id],LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(topBands[id],LV_OBJ_FLAG_SCROLLABLE);
        }
        for(unsigned r=0;r<4;++r)for(unsigned c=4;c<6;++c)create_key(6+r*6+c,c==4?420:512,60+r*96,88,88);
        guide=lv_obj_create(lv_screen_active());lv_obj_set_pos(guide,screenX,screenY);lv_obj_set_size(guide,screenSize,screenSize);
        lv_obj_set_style_pad_all(guide,0,0);lv_obj_remove_flag(guide,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(guide,lv_color_hex(0x101820),0);visible(guide,false);
        mapTitle=label(guide,8,8,398);
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)create_key(6+r*6+c,6+c*104,36+r*96,96,72,guide);
        mapDetail=label(guide,8,400,398);visible(mapDetail,false);
        mapSummary=label(guide,8,398,398);lv_obj_set_height(mapSummary,16);lv_label_set_long_mode(mapSummary,LV_LABEL_LONG_SCROLL_CIRCULAR);
        // Both the invisible hit targets and their expanded labels must stay above the canvas/guide.
        for(unsigned id=0;id<6;++id)lv_obj_move_foreground(keys[id]);
        refresh_top_keys();
        static const int order[]={1,2,3,4,5,6,0,7};
        for(unsigned i=0;i<8;++i){int id=order[i];targets[id]=tile("",608+(i%2)*96,108+(i/2)*72,84,64);
            lv_obj_add_event_cb(targets[id],knob,LV_EVENT_ALL,reinterpret_cast<void*>(intptr_t(id)));}
        status=label(lv_screen_active(),608,60,184);lv_obj_set_height(status,42);
        helpTitle=label(lv_screen_active(),608,392,184);helpSummary=label(lv_screen_active(),608,414,184);
        lv_obj_set_height(helpTitle,18);lv_obj_set_height(helpSummary,18);
        lv_label_set_long_mode(helpTitle,LV_LABEL_LONG_SCROLL_CIRCULAR);lv_label_set_long_mode(helpSummary,LV_LABEL_LONG_SCROLL_CIRCULAR);
        mapButton=tile("MAP AUTO",608,12,84,44);
        lv_obj_add_event_cb(mapButton,[](lv_event_t*){stop_drag();mapMode=(mapMode+1)%3;if(mapMode==0)touchKeys.release_all();zoom=false;redraw=true;},LV_EVENT_CLICKED,nullptr);
        zoomButton=tile("ZOOM",704,12,84,44);
        lv_obj_add_event_cb(zoomButton,[](lv_event_t*){stop_drag();touchKeys.release_all();zoom=!zoom;redraw=true;},LV_EVENT_CLICKED,nullptr);
        auto* save=tile("SD SAVE",608,441,84,34);
        lv_obj_add_event_cb(save,[](lv_event_t*){show_help(HelpKind::SD);storage_request();},LV_EVENT_CLICKED,nullptr);
        auto* route=tile("OUTPUT",704,441,84,34);
        lv_obj_add_event_cb(route,[](lv_event_t*){show_help(HelpKind::Route);p4_audio->set_route(P4Audio::Route((unsigned(p4_audio->route())+1)%3));},LV_EVENT_CLICKED,nullptr);
        auto decor=[](int w,int h,uint32_t colour){auto* o=lv_obj_create(lv_screen_active());lv_obj_set_size(o,w,h);
            lv_obj_set_style_bg_color(o,lv_color_hex(colour),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
            lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_pad_all(o,0,0);lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);visible(o,false);return o;};
        rail=decor(6,200,0x8ce0c8);origin=decor(20,2,0xffffff);marker=decor(14,14,0xffd000);
        railValue=label(lv_screen_active(),0,0,112);lv_obj_set_style_bg_color(railValue,lv_color_hex(0x101820),0);
        lv_obj_set_style_bg_opa(railValue,LV_OPA_COVER,0);lv_obj_set_style_text_color(railValue,lv_color_hex(0xffd000),0);
        lv_obj_remove_flag(railValue,LV_OBJ_FLAG_CLICKABLE);visible(railValue,false);
        int oldSize=screenSize,oldTarget=-1;int targetStyle[8];std::fill_n(targetStyle,8,-1);
        for(;;){bool received=false;
            if(xSemaphoreTake(exchange,0)==pdTRUE){
                if(fresh){shownHelp=pendingHelp;fresh=false;received=true;}
                bool nextMap=!zoom && (mapMode==2 || (mapMode==1 && (shownHelp.fn || shownHelp.layer!=0)));
                if(nextMap!=mapVisible){mapVisible=nextMap;redraw=true;}
                if((received || redraw) && !mapVisible){int size=zoom?480:screenSize;
                    if(size!=oldSize){lv_canvas_set_buffer(canvas,scaled,size,size,LV_COLOR_FORMAT_RGB565);oldSize=size;}
                    lv_obj_set_pos(canvas,zoom?0:screenX,zoom?0:screenY);
                    for(unsigned y=0;y<unsigned(size);++y)for(unsigned x=0;x<unsigned(size);++x)
                        scaled[y*size+x]=pending[(y*240/size)*240+x*240/size];
                    lv_obj_invalidate(canvas);
                }redraw=false;xSemaphoreGive(exchange);
            }
            if(adjust.id>=0 && (dragFn!=shownHelp.fn || !shownHelp.controls[adjust.id].enabled || adjust.context!=shownHelp.controls[adjust.id].context))stop_drag();
            visible(guide,mapVisible);visible(canvas,!mapVisible);
            for(unsigned id=0;id<30;++id)if(id<6 || id%6>=4)visible(keys[id],!zoom);
            bool update=received || oldTarget!=wheel_target.load();
            if(update)update_keys();
            refresh_top_keys();
            label_update(helpTitle,shownHelp.current.title);char brief[128];
            std::snprintf(brief,sizeof brief,"%s",shownHelp.current.summary);for(char* p=brief;*p;++p)if(*p=='\n')*p=' ';
            label_update(helpSummary,brief);
            char heading[128];std::snprintf(heading,sizeof heading,"%s%s",shownHelp.fn?"Fn + ":"",shownHelp.context);
            label_update(mapTitle,heading);label_update(mapDetail,shownHelp.current.title);label_update(mapSummary,brief);
            for(unsigned i=0;i<8;++i){char text[96];auto& c=shownHelp.controls[i];
                if(i>=3 && i<=6)std::snprintf(text,sizeof text,"K%u: %s\n%s",i-2,c.label,c.enabled?c.value:"--");
                else std::snprintf(text,sizeof text,"%s\n%s",c.label,c.enabled?c.value:"--");
                label_update(lv_obj_get_child(targets[i],0),text);
                int style=!c.enabled?0:wheel_target==int(i)?2:1;
                if(targetStyle[i]!=style){lv_obj_set_style_bg_color(targets[i],lv_color_hex(style==0?0x181e24:style==2?0x348875:0x30343c),0);
                    lv_obj_set_style_text_color(lv_obj_get_child(targets[i],0),lv_color_hex(c.enabled?0xffffff:0x8a98a5),0);targetStyle[i]=style;}
            }
            oldTarget=wheel_target.load();
            const char* modes[]={"MAP OFF","MAP AUTO","MAP ON"};label_update(lv_obj_get_child(mapButton,0),modes[mapMode]);
            label_update(lv_obj_get_child(zoomButton,0),zoom?"BACK":"ZOOM");
            const char* routes[]={"AUTO","ONBOARD","USB"};char text[128];
            std::snprintf(text,sizeof text,"%s%s %s\n%s / %s",playing?"PLAY":"STOP",shownHelp.recording==2?" REC":shownHelp.recording==1?" ARM":"",shownHelp.fn?"Fn":"",
                routes[unsigned(p4_audio->route())],storage_status());label_update(status,text);
            p4::panel_service();if(p4::panel_render_ready())lv_timer_handler();vTaskDelay(pdMS_TO_TICKS(5));
        }
    },"sloop-ui",12288,nullptr,3,nullptr,0)!=pdPASS)abort();
}
}
