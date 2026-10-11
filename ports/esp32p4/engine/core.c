/* SPDX-License-Identifier: GPL-3.0-only */
/* Additive platform wrapper. The unchanged firmware runs on a single worker:
 * no firmware state, queues or interrupt guards are shared between threads. */
#ifdef ESP_PLATFORM
#define HOST_LARGE __attribute__((section(".ext_ram.bss"), aligned(16)))
#else
#define HOST_LARGE __attribute__((aligned(16)))
#endif
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "core.h"
#ifdef __APPLE__
#define __attribute__(x)
#endif
#undef memset
#undef memcpy
#define memset sloop_memset
#define memcpy sloop_memcpy
#define memcmp sloop_memcmp
#include "libc.c"
#undef memset
#undef memcpy
#undef memcmp
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#define FELUCCA_OTA 0
#define FELUCCA_CDC 0
#define FELUCCA_UAC 0
#define FELUCCA_ICONS 1
static uint8_t host_flash[SLOOP_STORAGE_SIZE] HOST_LARGE;
static int storage_dirty;
#define SMP_USER_XIP(k) (host_flash + ((k) < 3u ? 0xA0000u + (k) * 0x14000u : 0xE7000u))
#define FM6_BANK_XIP(c) (host_flash + st_sector(OBJ_FM6BANK, c) + ST_PAYLOAD_OFF)
static uint8_t host_sample_byte(uint32_t off);
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
static struct { uint32_t notes, buttons; } fm1_in;
#include "felucca_tables.h"
#include "sloop_firmware/core.h"
#include "engines.c"
static uint8_t host_sample_byte(uint32_t off)
{
    if (!(off & 0x80000000u)) return off < sizeof SMP_DATA ? SMP_DATA[off] : 0;
    unsigned slot = (off >> 24) & 3u, pos = off & 0xFFFFFFu;
    return pos < SMP_USER_SIZE - SMP_USER_DATA ? SMP_USER_XIP(slot)[SMP_USER_DATA + pos] : 0;
}
#include "drums.c"
#include "params.c"
#include "voice.c"
#include "slicer.c"
#include "fx.c"
#define MQ 256u
static uint32_t midi_in_q[MQ], mi_w, mi_r;
static struct { uint8_t config, suspended, up; uint32_t setups, sof_seen; } usb;
static void midi_out_event(uint32_t packet) { (void)packet; } /* input port only */
static int usb_cdc_now(void) { return 0; }
#include "arranger.c"
#include "seq.c"
static uint16_t screen[240 * 240] HOST_LARGE;
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    for (unsigned j = 0; j < h && y + j < 240; j++)
        for (unsigned i = 0; i < w && x + i < 240; i++) screen[(y+j)*240+x+i] = p[j*w+i];
}
#include "gfx.c"
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    for (unsigned j = 0; j < h && y+j < 240; j++)
        for (unsigned i = 0; i < w && x+i < 240; i++) screen[(y+j)*240+x+i] = swap16(c);
}
static int32_t encs[7];
#define FM1_TICKS_PER_US 24u
static uint32_t fm1_ticks(void) { return fm1_ms * 24000u; }
static int32_t fm1_enc_take(uint32_t e) { int32_t v = encs[e]; encs[e] = 0; return v; }
static uint8_t fm1_led[16], fm1_led_dim[16], fm1_led_bg[16];
static volatile uint16_t fm1_led_bg_ns;
#define FM1_NCOL 16u
static const int8_t FM1_KEYMAP[5][16];
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }
static uint32_t edges_btn, notes_seen;
static uint32_t fm1_input_edges(int n) { (void)n; uint32_t v = edges_btn; edges_btn = 0; return v; }
static uint32_t fm1_input_note_edges(void) { uint32_t v = fm1_in.notes & ~notes_seen; notes_seen = fm1_in.notes; return v; }
static void fm1_wdt_feed(void) { fm1_ms++; }
static int32_t fm1_adc_read(int c) { (void)c; return -1; }
static struct { uint32_t magic, stage, page, home, ui_frames; } felucca_dbg;
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N], scope_bufr[SCOPE_N];
static uint32_t scope_w;
#include "panel.c"
static sloop_control_info host_knobs[4];
static void host_knob_info(unsigned c,const char *label,const char *value,const char *unit);
#include "ui.c"
#include "ui_song.c"
#include "ui_studio.c"
#include "icons.c"
#include "ui_draw.c"
#include "ui_vis.c"
#include "ui_layers.c"
#include "ui_menu.c"
#include "ui_input.c"
static uint8_t flash_ok;
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off > sizeof host_flash || n > sizeof host_flash - off) return -1;
    memcpy(dst, host_flash + off, n); return 0;
}
static int st_erase(uint32_t off)
{
    if (off > sizeof host_flash - 4096u || (off & 4095u)) return -1;
    memset(host_flash + off, 255, 4096); storage_dirty = 1; return 0;
}
static int st_prog(uint32_t off, const void *data, uint32_t n)
{
    if (off > sizeof host_flash || n > sizeof host_flash - off) return -1;
    memcpy(host_flash + off, data, n); storage_dirty = 1; return 0;
}
#include "storage.c"
#include "upreset.c"
#define FL_FAR(f) f
static uint32_t fl_jedec_ram(void) { return 0x856014u; }
static void fl_plain_window_init(void) {}
#include "project.c"
static int16_t pcm[CTL*2];
static unsigned pcm_pos = CTL;
void sloop_init(const uint8_t *image)
{
    memset(host_knobs,0,sizeof host_knobs);
    memset(&song, 0, sizeof song); memset(trk, 0, sizeof trk);
    memset(proj_slot, 0, sizeof proj_slot); memset(&persist_saved, 0, sizeof persist_saved);
    memset(&settings, 0, sizeof settings); memset(&ui, 0, sizeof ui);
    memset(eng_arena, 0, sizeof eng_arena); memset(eng_arena_own, 0, sizeof eng_arena_own);
    fm1_in.notes = fm1_in.buttons = edges_btn = notes_seen = kb_prev = 0;
    mi_r = mi_w = 0; clk_beat = clk_pos = 0; pcm_pos = CTL;
    if (image) memcpy(host_flash, image, sizeof host_flash);
    else memset(host_flash, 255, sizeof host_flash);
    panel = PANEL_DEFAULT;
    persist_boot(); panel_init(); settings_init(); fm6_init();
    for (unsigned i = 0; i < G_COUNT; i++) song.g[i] = GP[i].def;
    for (unsigned i = 0; i < NTRK; i++) {
        track_defaults(&trk[i]);
        if (i < NPART) { set_engine_of(&trk[i], TRK_DEF[i][0]); apply_preset_to(&trk[i], TRK_DEF[i][1]); trk[i].engine = trk[i].eng_req; }
        track_defaults_steps(&trk[i]);
    }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    song.master_q12 = 2048; song.batt_raw = 620;
    autosave_resume();
    song.g[G_SYNC] = lights_sync; song.g[G_MIDI] = lights_mout; song.g[G_ROUTE] = lights_min;
    layers_init(); go_home(); ui.force = 1; ui_draw(); storage_dirty = 0;
}
static void pcm_fill(void)
{
    static int32_t block[CTL*2];
    mix_block(block, CTL);
    for (unsigned i = 0; i < CTL*2; i++) pcm[i] = clamp(block[i], -32768, 32767);
    for (unsigned i = 1; i < CTL; i += 2) {
        scope_bufr[scope_w & (SCOPE_N-1)] = vis_tap[2*i+1];
        scope_buf[scope_w++ & (SCOPE_N-1)] = vis_tap[2*i];
    }
    pcm_pos = 0;
}
void sloop_render(float *out, unsigned frames)
{
    for (unsigned f = 0; f < frames; f++) {
        if (pcm_pos == CTL) pcm_fill();
        out[2*f] = pcm[2*pcm_pos] / 32768.0f;
        out[2*f+1] = pcm[2*pcm_pos+1] / 32768.0f;
        pcm_pos++;
    }
}
void sloop_event(int kind, int id, int value)
{
    if (kind == SLOOP_BUTTON && id >= 0 && id < NB) {
        uint32_t bit = 1u << panel.btn[id];
        if (value) { if (!(fm1_in.buttons & bit)) edges_btn |= bit; fm1_in.buttons |= bit; }
        else fm1_in.buttons &= ~bit;
        keyboard_block(); ui_input();
    } else if (kind == SLOOP_ENCODER && id >= 0 && id < NE) {
        encs[panel.enc[id]] += clamp(value, -32, 32); ui_input();
    } else if (kind == SLOOP_KEY && id >= 0 && id < 27) {
        if (value) fm1_in.notes |= 1u << id; else fm1_in.notes &= ~(1u << id);
        keyboard_block(); ui_input();
    } else if (kind == SLOOP_MIDI) {
        if (mi_w - mi_r < MQ) midi_in_q[mi_w++ % MQ] = (uint32_t)value;
        else sloop_event(SLOOP_PANIC, 0, 0);
    } else if (kind == SLOOP_PANIC) {
        fm1_in.notes = fm1_in.buttons = edges_btn = 0;
        mi_r = mi_w; memset(midi_sel_on, 0, sizeof midi_sel_on);
        panic_req = (1u << NTRK)-1u; keyboard_block(); ui_input();
    } else if (kind == SLOOP_MASTER) song.master_q12 = clamp(value, 0, 4096);
    else if (kind == SLOOP_STOP) transport_req = 2;
    else if (kind == SLOOP_CONNECTION) usb.config = usb.up = value != 0;
}
void sloop_tick(uint32_t milliseconds)
{
    fm1_ms = milliseconds; ui_input(); ui_leds(); ui_draw(); autosave_tick(); sections_flush();
}
void sloop_pixels(uint32_t *argb)
{
    for (unsigned i = 0; i < 240*240; i++) {
        uint16_t p = swap16(screen[i]);
        argb[i] = 0xFF000000u | (((p>>11)*255u/31u)<<16) | ((((p>>5)&63u)*255u/63u)<<8) | ((p&31u)*255u/31u);
    }
}
int sloop_playing(void) { return song.playing != 0; }
void sloop_checkpoint(void) { persist_flush_now(); }
const uint8_t *sloop_storage(void) { return host_flash; }
int sloop_storage_dirty(void) { return storage_dirty; }
void sloop_storage_clean(void) { storage_dirty = 0; }
int sloop_import_sample(unsigned slot, const uint8_t *data, unsigned size)
{
    if (slot >= 4 || size < SMP_USER_DATA || size > SMP_USER_SIZE || song.playing) return 0;
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)data;
    if (h->magic != SMP_USER_MAGIC || h->version != 1 || !h->nz || h->nz > 16 ||
        h->data_len > size-SMP_USER_DATA || st_crc32(data+SMP_USER_DATA, h->data_len) != h->crc) return 0;
    for (unsigned i = 0; i < h->nz; i++) if (!smp_zone_ok(&h->zone[i], h->data_len)) return 0;
    memset((void *)SMP_USER_XIP(slot), 255, SMP_USER_SIZE);
    memcpy((void *)SMP_USER_XIP(slot), data, size); smp_user_scan(slot); storage_dirty = 1; return 1;
}

void sloop_render_i16(int16_t *out, unsigned frames) {
    for(unsigned f=0;f<frames;++f) {
        if(pcm_pos==CTL) pcm_fill();
        out[2*f]=pcm[2*pcm_pos];out[2*f+1]=pcm[2*pcm_pos+1];++pcm_pos;
    }
}
void sloop_pixels565(uint16_t *out) {for(unsigned i=0;i<240*240;++i)out[i]=swap16(screen[i]);}
void sloop_lights(uint32_t *out) {for(unsigned i=0;i<16;++i)out[i]=fm1_led[i]?0x60c080:fm1_led_dim[i]?0x143020:0x080c08;}
int sloop_quiet(void) {return !song.playing && !fm1_in.notes && !fm1_in.buttons && audio_quiet();}
void sloop_key_lights(uint32_t *out) {
    uint32_t on=keys_lit(),dim=keys_notes_dim();
    for(unsigned i=0;i<27;++i)out[i]=(on&(1u<<i))?0x60c080:(dim&(1u<<i))?0x183028:0x080c08;
}

void sloop_help_context(char *text,unsigned size) {
    const char *name=layer_now()==LY_PLAY?(kb_grid?"DRUM GRID":"PLAY"):LAYER_NAME[layer_now()];
    snprintf(text,size,"Track %u / %s%s",song.sel%NTRK+1,name,ly_lock!=LY_PLAY?" (locked)":"");
}
void sloop_key_hint(unsigned key,char *title,unsigned ts,char *summary,unsigned ss) {
    if(key>=27)return;
    track_t *t=TSEL;uint32_t layer=layer_now();int w=punch_key(key);
    const char *label=NULL,*description="No action on this layer.";
    char number[32];
    if(layer==LY_SCALE){snprintf(number,sizeof number,"Key: %s",N_NOTE[(53+key)%12]);label=number;description="Set the root key of all synth tracks.";}
    else if(layer==LY_FX && w>=0){label=PUNCH_NAME[w];description="Hold this pad to punch in the effect.";}
    else if(layer==LY_STEP || (layer==LY_PLAY && is_drum(t) && kb_grid)){
        if(w>=0){snprintf(number,sizeof number,"Step %u",ui.step_page*16u+w+1);label=number;description="Tap: set/clear. Hold + knob: edit.";}
        else if(key==1 || key==3 || key==5 || key==8){unsigned pg=key==1?1:key==3?2:key==5?3:4;
            snprintf(number,sizeof number,"Steps page %u",pg);label=number;description="Show this bank of sixteen steps.";}
    }else if(layer==LY_MIX && w>=0){
        if(w<8){snprintf(number,sizeof number,"%s track %u",w<4?"Mute":"Solo",(w%4)+1);label=number;description=w<4?"Toggle this track in the mix.":"Hear this track on its own.";}
        else if(w==8){label="Fill";description="Hold for fill-only sequencer steps.";}
        else if(w==9){label="Fill next bar";description="Toggle a fill for the next bar.";}
        else if(w==15){label="Tap tempo";description="Tap in time to set the song tempo.";}
    }else if(layer==LY_SONG && w>=0){
        if(w<8){snprintf(number,sizeof number,"%s section %c",w<4?"Play":"Store",'A'+w%4);label=number;description=w<4?"Queue a section; several taps form a chain.":"Store the current loop in this section.";}
        else if(w==12){label="Loop / song";description="Switch looping and song playback.";}
        else if(w==13){label="Song record";description="Record the order of played sections.";}
        else if(w==15){label="Song screen";description="Open the section arrangement.";}
    }else if(layer==LY_PLAY || layer==LY_ROLL || layer==LY_ERASE){
        if(is_drum(t)){snprintf(number,sizeof number,"%s",LANE_NAME[lane_of_key(key)]);label=number;}
        else {uint32_t note=kb_map(t,key);
            if(note==KB_SILENT){label="Outside scale";description="This key is silent in the current scale.";}
            else {snprintf(number,sizeof number,"%s%d%s",N_NOTE[note%12],(int)(note/12)-1,t->p[P_CHORD]?" chord":"");label=number;}}
        if(label!=NULL && strcmp(label,"Outside scale"))description=layer==LY_ERASE?"Erase this note/sound from the pattern.":layer==LY_ROLL?"Hold for rhythmic repeats of this sound.":"Play this sound on the selected track.";
    }
    snprintf(title,ts,"%s",label?label:"Unassigned");snprintf(summary,ss,"%s",description);
}

unsigned sloop_active_layer(void) { return layer_now(); }
int sloop_locked_button(void) { return ly_lock==LY_PLAY?-1:LAYER_BTN[ly_lock%LY_COUNT]; }
int sloop_recording(void) { return song.rec?2:rec_wait?1:0; }
uint32_t sloop_encoder_context(unsigned id) {
    /* Identity, not value: a queued drag must never cross into another parameter.
     * Browsing encoders omit the state they themselves select. */
    uint32_t layer=layer_now(),h=2166136261u;
#define HC(v) h=(h^(uint32_t)(v))*16777619u
    HC(id);HC(layer);HC(ly_lock);HC(ui.menu);HC(ui.hold_kind);HC(ui.confirm);HC(ft_on);HC(rec_wait);HC(vis_shown());
    if(ui.menu){if(id!=EN_SELECT)HC(mi_sec(ui.menu_sel%MI_COUNT));return h?h:1;}
    if(id==EN_SELECT){HC(ui.home);HC(cur_page()->fam);HC(cur_page()->scope==SC_SONG);HC(cur_page()->scope==SC_DRUM);}
    else if(id==EN_ALGO){if(layer==LY_STEP && ui.step_held){HC(song.sel);HC(ui.step_held);HC(ui.step_page);}}
    else {HC(song.sel);
        if(id==EN_PRESET){HC(ui.home);HC(cur_page()->graph==GR_BROWSE);HC(cur_fam()==FAM_TRK);
            if(layer==LY_STEP && ui.step_held){HC(ui.step_held);HC(ui.step_page);HC(ui.lock_par);}}
        else {HC(ui.page);HC(ui.home);HC(TSEL->eng_req);HC(ui.step_held);HC(ui.step_page);
            if(id!=EN_K1){HC(ui.cursor);HC(pen_lane);}
        }
    }
#undef HC
    return h?h:1;
}
static void host_knob_info(unsigned c,const char *label,const char *value,const char *unit) {
    if(c>=4)return;sloop_control_info *s=&host_knobs[c];
    snprintf(s->label,sizeof s->label,"%s",label?label:"");
    snprintf(s->value,sizeof s->value,"%s%s",value?value:"",unit?unit:"");
    s->context=sloop_encoder_context(EN_K1+c);s->enabled=label && label[0] && label[0]!='-';
}
static void host_control(sloop_control_info *s,const char *label,const char *value,int enabled,unsigned id) {
    snprintf(s->label,sizeof s->label,"%s",label);snprintf(s->value,sizeof s->value,"%s",value);
    s->enabled=enabled;s->context=sloop_encoder_context(id);
}
void sloop_controls_snapshot(sloop_control_info out[8]) {
    uint32_t layer=layer_now();int blocked=ui.menu || ui.hold_kind || ui.confirm;
    int held_step=layer==LY_STEP && ui.step_held;char value[32];
    if(ui.menu){unsigned sec=mi_sec(ui.menu_sel%MI_COUNT),n=MS_FIRST[sec+1]-MS_FIRST[sec];
        host_control(&out[0],"SECTION",MS_NAME[sec],ui.menu==1,EN_SELECT);
        host_control(&out[1],"TRACK","--",0,EN_ALGO);
        host_control(&out[2],"MENU ROW",MI_NAME[ui.menu_sel%MI_COUNT],ui.menu==1,EN_PRESET);
        for(unsigned k=0;k<4;++k){uint16_t colour;unsigned item=MS_FIRST[sec]+k;
            host_control(&out[3+k],k<n?MI_NAME[item]:"--",k<n?mi_value(item,&colour):"--",ui.menu==1 && k<n,EN_K1+k);}
        memset(&out[7],0,sizeof out[7]);snprintf(out[7].label,sizeof out[7].label,"MASTER");out[7].enabled=1;out[7].context=1;
        return;
    }
    snprintf(value,sizeof value,"%ld BPM",(long)song.g[G_BPM]);
    const char *select="TEMPO";
    if(vis_shown() && !rec_wait && !ft_on && layer==LY_PLAY){select="VISUAL";value[0]=0;}
    else if(layer==LY_PLAY && !ui.home && !rec_wait && !ft_on && cur_page()->scope!=SC_SONG && cur_page()->scope!=SC_DRUM){
        unsigned n=0;for(unsigned i=0;i<NPAGES;++i)if(PAGES[i].fam==cur_page()->fam && PAGES[i].scope!=SC_SONG && PAGES[i].scope!=SC_DRUM)++n;
        if(n>1){select="PAGE";snprintf(value,sizeof value,"%s",cur_page()->title);}
    }
    host_control(&out[0],select,value,!blocked,EN_SELECT);
    snprintf(value,sizeof value,"Track %u",song.sel+1);
    host_control(&out[1],held_step?"LOCK PARAM":"TRACK",held_step?lock_label(TSEL,ui.lock_par):value,
        !blocked && !ft_on && (layer==LY_PLAY || held_step),EN_ALGO);
    snprintf(value,sizeof value,"%s",is_drum(TSEL)?"Drum kit":ENGINES[TSEL->eng_req%NENGINES]->name);
    if(held_step){unsigned w=0;while(w<16 && !(ui.step_held&(1u<<w)))++w;
        int k=lock_find(TSEL,(ui.step_page*16+w)%NSTEP,ui.lock_par,0);
        if(k>=0){char v[16];const char *u;param_format(lock_desc(TSEL,ui.lock_par),TSEL->lock[k].val,v,&u);snprintf(value,sizeof value,"%s%s",v,u);}
        else snprintf(value,sizeof value,"--");
    }
    host_control(&out[2],held_step?"LOCK VALUE":is_drum(TSEL)?"KIT":"SOUND",value,
        !blocked && !ft_on && (held_step || (layer==LY_PLAY && (ui.home || cur_page()->graph==GR_BROWSE || cur_fam()==FAM_TRK))),EN_PRESET);
    for(unsigned i=0;i<4;++i){out[i+3]=host_knobs[i];uint32_t context=sloop_encoder_context(EN_K1+i);
        if(out[i+3].context!=context || blocked || (layer==LY_PLAY && vis_shown() && !rec_wait && !ft_on)){
            out[i+3].enabled=0;out[i+3].value[0]=0;
            snprintf(out[i+3].label,sizeof out[i+3].label,"WAIT");
        }out[i+3].context=context;
        if(layer==LY_PLAY && ly_quiet())out[i+3].enabled=0;
    }
    memset(&out[7],0,sizeof out[7]);snprintf(out[7].label,sizeof out[7].label,"MASTER");out[7].enabled=1;out[7].context=1;
}
int sloop_encoder_guarded(unsigned id,int delta,uint32_t context) {
    if(id>=7 || context!=sloop_encoder_context(id))return 0;
    sloop_control_info info[8];sloop_controls_snapshot(info);if(!info[id].enabled)return 0;
    /* Native track selection interprets the sign of each event, not its size. */
    while(delta){int n=(id==EN_ALGO || ui.menu)?(delta>0?1:-1):clamp(delta,-32,32);
        if(context!=sloop_encoder_context(id))break;sloop_event(SLOOP_ENCODER,id,n);delta-=n;
    }return 1;
}
