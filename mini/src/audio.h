#pragma once
#include <stdbool.h>

typedef struct {
    bool has_audio;    /* mixer opened successfully */
    bool muted;
    int  volume_pct;   /* 0..100 (we don't currently surface boosts >100) */
} audio_state_t;

/* Open the ALSA "default" mixer and locate a master playback element.
 * On systems where ALSA is routed through PipeWire this just works through
 * the alsa_pcm_pipewire plugin. Safe no-op when libasound finds nothing. */
void audio_init(void);
void audio_close(void);

/* Refresh and return the latest state. Cheap; safe to call on every slow
 * tick (~1 s). */
void audio_poll(audio_state_t *out);
