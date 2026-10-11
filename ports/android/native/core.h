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
