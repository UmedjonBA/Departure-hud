#define _GNU_SOURCE
#include "audio.h"

#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    snd_mixer_t       *handle;
    snd_mixer_elem_t  *elem;
    long               vmin, vmax;
    bool               opened;
} A;

/* Pick the first usable Master-like element. Different distros expose
 * different names: PulseAudio/PipeWire usually present a "Master" mixer via
 * their ALSA plugin; bare-metal cards often use "PCM" or "Headphone". */
static snd_mixer_elem_t *find_master(snd_mixer_t *h) {
    static const char *names[] = {
        "Master", "PCM", "Playback", "Speaker", "Headphone", NULL
    };
    snd_mixer_selem_id_t *sid;
    if (snd_mixer_selem_id_malloc(&sid) < 0) return NULL;
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_elem_t *picked = NULL;
    for (int i = 0; names[i]; i++) {
        snd_mixer_selem_id_set_name(sid, names[i]);
        snd_mixer_elem_t *e = snd_mixer_find_selem(h, sid);
        if (e && snd_mixer_selem_has_playback_volume(e)) { picked = e; break; }
    }
    /* Fallback: first element with a playback volume. */
    if (!picked) {
        for (snd_mixer_elem_t *e = snd_mixer_first_elem(h); e;
             e = snd_mixer_elem_next(e)) {
            if (snd_mixer_selem_is_active(e) &&
                snd_mixer_selem_has_playback_volume(e)) {
                picked = e; break;
            }
        }
    }
    snd_mixer_selem_id_free(sid);
    return picked;
}

void audio_init(void) {
    if (A.opened) return;
    if (snd_mixer_open(&A.handle, 0) < 0) return;
    if (snd_mixer_attach(A.handle, "default") < 0) goto fail;
    if (snd_mixer_selem_register(A.handle, NULL, NULL) < 0) goto fail;
    if (snd_mixer_load(A.handle) < 0) goto fail;

    A.elem = find_master(A.handle);
    if (!A.elem) goto fail;

    snd_mixer_selem_get_playback_volume_range(A.elem, &A.vmin, &A.vmax);
    if (A.vmax <= A.vmin) goto fail;
    A.opened = true;
    return;

fail:
    if (A.handle) { snd_mixer_close(A.handle); A.handle = NULL; }
    A.elem = NULL;
}

void audio_close(void) {
    if (A.handle) snd_mixer_close(A.handle);
    A.handle = NULL;
    A.elem   = NULL;
    A.opened = false;
}

void audio_poll(audio_state_t *out) {
    out->has_audio  = false;
    out->volume_pct = 0;
    out->muted      = false;
    if (!A.opened) return;

    /* Drain pending events so subsequent reads see fresh values. */
    snd_mixer_handle_events(A.handle);

    long v = 0;
    if (snd_mixer_selem_get_playback_volume(
            A.elem, SND_MIXER_SCHN_FRONT_LEFT, &v) < 0) {
        return;
    }
    int pct = (int)((v - A.vmin) * 100 / (A.vmax - A.vmin));
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    int sw = 1;
    if (snd_mixer_selem_has_playback_switch(A.elem)) {
        snd_mixer_selem_get_playback_switch(
            A.elem, SND_MIXER_SCHN_FRONT_LEFT, &sw);
    }

    out->has_audio  = true;
    out->volume_pct = pct;
    out->muted      = !sw;
}
