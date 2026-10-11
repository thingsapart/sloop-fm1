/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* All functions are called by one engine worker, including UI and persistence. */
void sloop_init(const uint8_t *image);
void sloop_render(float *out, unsigned frames);
void sloop_event(int kind, int id, int value);
void sloop_tick(uint32_t milliseconds);
void sloop_pixels(uint32_t *argb);
int sloop_playing(void);
void sloop_checkpoint(void);
const uint8_t *sloop_storage(void);
int sloop_storage_dirty(void);
void sloop_storage_clean(void);
int sloop_import_sample(unsigned slot, const uint8_t *data, unsigned size);
#define SLOOP_STORAGE_SIZE 1048576u
/* Events: button, encoder, physical key, MIDI packet, panic, master, connection. */
enum { SLOOP_BUTTON, SLOOP_ENCODER, SLOOP_KEY, SLOOP_MIDI, SLOOP_PANIC,
       SLOOP_MASTER, SLOOP_CONNECTION, SLOOP_STOP };
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
void sloop_render_i16(int16_t *out, unsigned frames);
void sloop_pixels565(uint16_t *out);
void sloop_lights(uint32_t *out);
int sloop_quiet(void);
void sloop_key_lights(uint32_t *out);
#ifdef __cplusplus
}
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* Read-only help snapshots; call only from the SLOOP owner. */
void sloop_key_hint(unsigned key,char *title,unsigned title_size,char *summary,unsigned summary_size);
void sloop_help_context(char *text,unsigned size);
typedef struct { char label[24],value[32];uint32_t context;uint8_t enabled; } sloop_control_info;
void sloop_controls_snapshot(sloop_control_info controls[8]);
uint32_t sloop_encoder_context(unsigned encoder);
int sloop_encoder_guarded(unsigned encoder,int delta,uint32_t context);
unsigned sloop_active_layer(void);
int sloop_locked_button(void);
int sloop_recording(void);
#ifdef __cplusplus
}
#endif
