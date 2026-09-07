/* hexter DX7 software synthesizer - CLAP plugin
 *
 * Copyright (C) 2026 Keith Adler.
 * hexter is copyright (C) 2004-2018 Sean Bolton and others.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

#include <clap/clap.h>

#include "hexter_engine.h"

#define HEXTER_CLAP_ID   "com.github.keithadler.hexter"
#define MAX_EVENTS       4096
#define DEFAULT_RATE     48000.0f

enum {
    P_TUNING = 0,
    P_VOLUME,
    P_POLYPHONY,
    P_MONO_MODE,
    P_PROGRAM,
    P_COUNT
};

static const char *mono_mode_names[4] = { "Poly", "Mono", "Mono legato", "Mono both" };

typedef struct {
    clap_plugin_t              plugin;
    const clap_host_t         *host;
    const clap_host_params_t  *host_params;
    const clap_host_state_t   *host_state;
    const clap_host_log_t     *host_log;
    const clap_host_preset_load_t *host_preset_load;

    hexter_engine_t *engine;
    float            sample_rate;
    uint32_t         max_frames;
    float           *mono;            /* scratch, max_frames */
    hexter_event_t   events[MAX_EVENTS];

    int              last_program;    /* last program reported to the host */
    bool             active;
    bool             rescan_requested;
    bool             dirty_requested;
} hexter_clap_t;

static void
log_msg(hexter_clap_t *h, clap_log_severity sev, const char *msg)
{
    if (h->host_log) h->host_log->log(h->host, sev, msg);
    else fprintf(stderr, "hexter: %s\n", msg);
}

/* ---- descriptor ---- */

static const char *hexter_features[] = {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_STEREO,
    NULL
};

static const clap_plugin_descriptor_t hexter_desc = {
    .clap_version = CLAP_VERSION_INIT,
    .id           = HEXTER_CLAP_ID,
    .name         = "hexter",
    .vendor       = "Sean Bolton and Keith Adler",
    .url          = "https://github.com/keithadler/hexter",
    .manual_url   = "https://github.com/keithadler/hexter#readme",
    .support_url  = "https://github.com/keithadler/hexter/issues",
    .version      = HEXTER_ENGINE_VERSION,
    .description  = "Yamaha DX7 modeling FM synthesizer. Loads DX7 banks and answers to DX7 sysex.",
    .features     = hexter_features,
};

/* ---- engine lifetime helpers ---- */

static bool
recreate_engine(hexter_clap_t *h, float sample_rate)
{
    uint8_t *state = NULL;
    size_t   n = 0;
    hexter_engine_t *e;

    if (h->engine) {
        state = (uint8_t *)malloc(HEXTER_STATE_SIZE);
        if (state) n = hexter_engine_state_save(h->engine, state, HEXTER_STATE_SIZE);
    }
    e = hexter_engine_new(sample_rate);
    if (!e) {
        free(state);
        return false;
    }
    if (state && n) hexter_engine_state_load(e, state, n);
    free(state);
    if (h->engine) hexter_engine_free(h->engine);
    h->engine = e;
    h->sample_rate = sample_rate;
    return true;
}

/* ---- audio ports ---- */

static uint32_t
audio_ports_count(const clap_plugin_t *plugin, bool is_input)
{
    return is_input ? 0 : 1;
}

static bool
audio_ports_get(const clap_plugin_t *plugin, uint32_t index, bool is_input,
                clap_audio_port_info_t *info)
{
    if (is_input || index != 0) return false;
    info->id = 0;
    snprintf(info->name, sizeof(info->name), "%s", "Output");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

static const clap_plugin_audio_ports_t ext_audio_ports = {
    .count = audio_ports_count,
    .get   = audio_ports_get,
};

/* ---- note ports ---- */

static uint32_t
note_ports_count(const clap_plugin_t *plugin, bool is_input)
{
    return is_input ? 1 : 0;
}

static bool
note_ports_get(const clap_plugin_t *plugin, uint32_t index, bool is_input,
               clap_note_port_info_t *info)
{
    if (!is_input || index != 0) return false;
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect  = CLAP_NOTE_DIALECT_MIDI;
    snprintf(info->name, sizeof(info->name), "%s", "MIDI In");
    return true;
}

static const clap_plugin_note_ports_t ext_note_ports = {
    .count = note_ports_count,
    .get   = note_ports_get,
};

/* ---- params ---- */

static uint32_t
params_count(const clap_plugin_t *plugin)
{
    return P_COUNT;
}

static bool
params_get_info(const clap_plugin_t *plugin, uint32_t index, clap_param_info_t *info)
{
    if (index >= P_COUNT) return false;
    memset(info, 0, sizeof(*info));
    info->id = index;
    info->cookie = NULL;
    info->module[0] = 0;

    switch (index) {
      case P_TUNING:
        snprintf(info->name, sizeof(info->name), "Tuning");
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        info->min_value = HEXTER_ENGINE_TUNING_MIN;
        info->max_value = HEXTER_ENGINE_TUNING_MAX;
        info->default_value = HEXTER_ENGINE_TUNING_DEFAULT;
        break;
      case P_VOLUME:
        snprintf(info->name, sizeof(info->name), "Volume");
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        info->min_value = HEXTER_ENGINE_VOLUME_MIN;
        info->max_value = HEXTER_ENGINE_VOLUME_MAX;
        info->default_value = HEXTER_ENGINE_VOLUME_DEFAULT;
        break;
      case P_POLYPHONY:
        snprintf(info->name, sizeof(info->name), "Polyphony");
        info->flags = CLAP_PARAM_IS_STEPPED;
        info->min_value = 1;
        info->max_value = HEXTER_ENGINE_MAX_POLYPHONY;
        info->default_value = HEXTER_ENGINE_DEFAULT_POLYPHONY;
        break;
      case P_MONO_MODE:
        snprintf(info->name, sizeof(info->name), "Voice mode");
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
        info->min_value = 0;
        info->max_value = 3;
        info->default_value = 0;
        break;
      case P_PROGRAM:
        snprintf(info->name, sizeof(info->name), "Program");
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_ENUM;
        info->min_value = 0;
        info->max_value = 127;
        info->default_value = 0;
        break;
    }
    return true;
}

static bool
params_get_value(const clap_plugin_t *plugin, clap_id id, double *value)
{
    hexter_clap_t *h = (hexter_clap_t *)plugin->plugin_data;

    switch (id) {
      case P_TUNING:    *value = hexter_engine_get_tuning(h->engine);    return true;
      case P_VOLUME:    *value = hexter_engine_get_volume(h->engine);    return true;
      case P_POLYPHONY: *value = hexter_engine_get_polyphony(h->engine); return true;
      case P_MONO_MODE: *value = hexter_engine_get_mono_mode(h->engine); return true;
      case P_PROGRAM:   *value = hexter_engine_get_program(h->engine);   return true;
      default: return false;
    }
}

static bool
params_value_to_text(const clap_plugin_t *plugin, clap_id id, double value,
                     char *out, uint32_t out_size)
{
    hexter_clap_t *h = (hexter_clap_t *)plugin->plugin_data;
    char name[11];
    int v;

    switch (id) {
      case P_TUNING:
        snprintf(out, out_size, "%.1f Hz", value);
        return true;
      case P_VOLUME:
        snprintf(out, out_size, "%.1f dB", value);
        return true;
      case P_POLYPHONY:
        v = (int)lrint(value);
        snprintf(out, out_size, "%d voice%s", v, v == 1 ? "" : "s");
        return true;
      case P_MONO_MODE:
        v = (int)lrint(value);
        if (v < 0) v = 0;
        if (v > 3) v = 3;
        snprintf(out, out_size, "%s", mono_mode_names[v]);
        return true;
      case P_PROGRAM:
        v = (int)lrint(value);
        if (v < 0) v = 0;
        if (v > 127) v = 127;
        hexter_engine_get_program_name(h->engine, v, name);
        snprintf(out, out_size, "%d: %s", v + 1, name);
        return true;
      default:
        return false;
    }
}

static bool
params_text_to_value(const clap_plugin_t *plugin, clap_id id, const char *text,
                     double *value)
{
    int i;

    if (id >= P_COUNT) return false;
    if (id == P_MONO_MODE) {
        int best = -1;
        size_t best_len = 0;
        for (i = 0; i < 4; i++) {
            size_t len = strlen(mono_mode_names[i]);
            if (!strncmp(text, mono_mode_names[i], len) && len > best_len) {
                best = i;
                best_len = len;
            }
        }
        if (best >= 0) {
            *value = best;
            return true;
        }
    }
    if (id == P_PROGRAM) {
        /* accept "12: NAME" or "12" as one-based, matching the display */
        char *end;
        double d = strtod(text, &end);
        if (end == text) return false;
        *value = d - 1.0;
        return true;
    }
    {
        char *end;
        double d = strtod(text, &end);
        if (end == text) return false;
        *value = d;
        return true;
    }
}

/* apply a parameter change now (main thread, or audio thread outside render) */
static void
apply_param_now(hexter_clap_t *h, clap_id id, double value)
{
    switch (id) {
      case P_TUNING:    hexter_engine_set_tuning(h->engine, (float)value); break;
      case P_VOLUME:    hexter_engine_set_volume(h->engine, (float)value); break;
      case P_POLYPHONY: hexter_engine_set_polyphony(h->engine, (int)lrint(value)); break;
      case P_MONO_MODE: hexter_engine_set_mono_mode(h->engine, (int)lrint(value)); break;
      case P_PROGRAM:   hexter_engine_select_program(h->engine, (int)lrint(value)); break;
      default: break;
    }
}

static void
params_flush(const clap_plugin_t *plugin, const clap_input_events_t *in,
             const clap_output_events_t *out)
{
    hexter_clap_t *h = (hexter_clap_t *)plugin->plugin_data;
    uint32_t n = in->size(in), i;

    for (i = 0; i < n; i++) {
        const clap_event_header_t *hdr = in->get(in, i);
        if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID && hdr->type == CLAP_EVENT_PARAM_VALUE) {
            const clap_event_param_value_t *ev = (const clap_event_param_value_t *)hdr;
            apply_param_now(h, ev->param_id, ev->value);
        }
    }
}

static const clap_plugin_params_t ext_params = {
    .count         = params_count,
    .get_info      = params_get_info,
    .get_value     = params_get_value,
    .value_to_text = params_value_to_text,
    .text_to_value = params_text_to_value,
    .flush         = params_flush,
};

/* ---- state ---- */

static bool
state_save(const clap_plugin_t *plugin, const clap_ostream_t *stream)
{
    hexter_clap_t *h = (hexter_clap_t *)plugin->plugin_data;
    uint8_t buf[HEXTER_STATE_SIZE];
    size_t n = hexter_engine_state_save(h->engine, buf, sizeof(buf));
    size_t done = 0;

    if (!n) return false;
    while (done < n) {
        int64_t w = stream->write(stream, buf + done, n - done);
        if (w <= 0) return false;
        done += (size_t)w;
    }
    return true;
}

static bool
state_load(const clap_plugin_t *plugin, const clap_istream_t *stream)
{
    hexter_clap_t *h = (hexter_clap_t *)plugin->plugin_data;
    uint8_t buf[HEXTER_STATE_SIZE];
    size_t done = 0;

    while (done < sizeof(buf)) {
        int64_t r = stream->read(stream, buf + done, sizeof(buf) - done);
        if (r < 0) return false;
        if (r == 0) break;
        done += (size_t)r;
    }
    if (!hexter_engine_state_load(h->engine, buf, done)) {
        log_msg(h, CLAP_LOG_WARNING, "state did not load: not a hexter state block");
        return false;
    }
    h->last_program = hexter_engine_get_program(h->engine);
    h->rescan_requested = true;
    h->host->request_callback(h->host);
    return true;
}

static const clap_plugin_state_t ext_state = {
    .save = state_save,
    .load = state_load,
};

/* ---- preset load: a DX7 bank file is a preset ---- */

static bool
preset_from_location(const clap_plugin_t *plugin, uint32_t location_kind,
                     const char *location, const char *load_key)
{
    hexter_clap_t *h = (hexter_clap_t *)plugin->plugin_data;
    char *err = NULL;
    int count;

    if (location_kind != CLAP_PRESET_DISCOVERY_LOCATION_FILE || !location) {
        if (h->host_preset_load)
            h->host_preset_load->on_error(h->host, location_kind, location, load_key,
                                          -1, "hexter loads banks from files only");
        return false;
    }
    count = hexter_engine_load_bank_file(h->engine, location, 0, &err);
    if (!count) {
        if (h->host_preset_load)
            h->host_preset_load->on_error(h->host, location_kind, location, load_key,
                                          -1, err ? err : "could not load bank");
        free(err);
        return false;
    }
    free(err);
    if (load_key && *load_key) {
        int p = atoi(load_key);
        if (p >= 1 && p <= 128) hexter_engine_select_program(h->engine, p - 1);
    }
    h->last_program = hexter_engine_get_program(h->engine);
    if (h->host_preset_load)
        h->host_preset_load->loaded(h->host, location_kind, location, load_key);
    h->rescan_requested = true;
    h->dirty_requested = true;
    h->host->request_callback(h->host);
    return true;
}

static const clap_plugin_preset_load_t ext_preset_load = {
    .from_location = preset_from_location,
};

/* ---- plugin ---- */

static bool
plugin_init(const clap_plugin_t *plugin)
{
    hexter_clap_t *h = (hexter_clap_t *)plugin->plugin_data;

    h->host_params = (const clap_host_params_t *)h->host->get_extension(h->host, CLAP_EXT_PARAMS);
    h->host_state  = (const clap_host_state_t *)h->host->get_extension(h->host, CLAP_EXT_STATE);
    h->host_log    = (const clap_host_log_t *)h->host->get_extension(h->host, CLAP_EXT_LOG);
    h->host_preset_load = (const clap_host_preset_load_t *)h->host->get_extension(h->host, CLAP_EXT_PRESET_LOAD);
    if (!h->host_preset_load)
        h->host_preset_load = (const clap_host_preset_load_t *)h->host->get_extension(h->host, CLAP_EXT_PRESET_LOAD_COMPAT);

    if (!recreate_engine(h, DEFAULT_RATE)) return false;
    hexter_engine_load_bank_from_env(h->engine);
    h->last_program = hexter_engine_get_program(h->engine);
    return true;
}

static void
plugin_destroy(const clap_plugin_t *plugin)
{
    hexter_clap_t *h = (hexter_clap_t *)plugin->plugin_data;

    if (h->engine) hexter_engine_free(h->engine);
    free(h->mono);
    free(h);
}

static bool
plugin_activate(const clap_plugin_t *plugin, double sample_rate,
                uint32_t min_frames, uint32_t max_frames)
{
    hexter_clap_t *h = (hexter_clap_t *)plugin->plugin_data;

    if ((float)sample_rate != h->sample_rate) {
        if (!recreate_engine(h, (float)sample_rate)) return false;
    }
    free(h->mono);
    h->max_frames = max_frames ? max_frames : 1;
    h->mono = (float *)calloc(h->max_frames, sizeof(float));
    if (!h->mono) return false;
    hexter_engine_reset(h->engine);
    h->active = true;
    return true;
}

static void
plugin_deactivate(const clap_plugin_t *plugin)
{
    hexter_clap_t *h = (hexter_clap_t *)plugin->plugin_data;
    h->active = false;
}

static bool
plugin_start_processing(const clap_plugin_t *plugin)
{
    return true;
}

static void
plugin_stop_processing(const clap_plugin_t *plugin)
{
}

static void
plugin_reset(const clap_plugin_t *plugin)
{
    hexter_clap_t *h = (hexter_clap_t *)plugin->plugin_data;
    hexter_engine_reset(h->engine);
}

static inline uint8_t
vel7(double v)
{
    int i = (int)lrint(v * 127.0);
    return (uint8_t)(i < 0 ? 0 : (i > 127 ? 127 : i));
}

static inline uint32_t
midi_length(uint8_t status)
{
    switch (status & 0xF0) {
      case 0xC0: case 0xD0: return 2;
      case 0xF0: return 1;
      default: return 3;
    }
}

static clap_process_status
plugin_process(const clap_plugin_t *plugin, const clap_process_t *process)
{
    hexter_clap_t *h = (hexter_clap_t *)plugin->plugin_data;
    const clap_input_events_t *in = process->in_events;
    uint32_t nframes = process->frames_count;
    uint32_t nin = in ? in->size(in) : 0;
    uint32_t ne = 0, i;
    int program_now;

    if (nframes > h->max_frames) nframes = h->max_frames;

    for (i = 0; i < nin && ne < MAX_EVENTS; i++) {
        const clap_event_header_t *hdr = in->get(in, i);
        hexter_event_t ev;
        uint32_t t = hdr->time > nframes ? nframes : hdr->time;

        if (hdr->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        memset(&ev, 0, sizeof(ev));
        ev.frame = t;

        switch (hdr->type) {
          case CLAP_EVENT_NOTE_ON: {
            const clap_event_note_t *n = (const clap_event_note_t *)hdr;
            if (n->key < 0) break;
            ev.type = HEXTER_EV_NOTE_ON;
            ev.a = (uint8_t)n->key;
            ev.b = vel7(n->velocity);
            if (ev.b == 0) ev.b = 1;
            h->events[ne++] = ev;
            break;
          }
          case CLAP_EVENT_NOTE_OFF:
          case CLAP_EVENT_NOTE_CHOKE: {
            const clap_event_note_t *n = (const clap_event_note_t *)hdr;
            if (n->key < 0) {
                ev.type = HEXTER_EV_CONTROL_CHANGE;
                ev.a = hdr->type == CLAP_EVENT_NOTE_CHOKE ? 120 : 123;  /* all sounds / notes off */
                ev.b = 0;
            } else {
                ev.type = HEXTER_EV_NOTE_OFF;
                ev.a = (uint8_t)n->key;
                ev.b = hdr->type == CLAP_EVENT_NOTE_CHOKE ? 127 : vel7(n->velocity);
            }
            h->events[ne++] = ev;
            break;
          }
          case CLAP_EVENT_NOTE_EXPRESSION: {
            const clap_event_note_expression_t *x = (const clap_event_note_expression_t *)hdr;
            if (x->expression_id == CLAP_NOTE_EXPRESSION_PRESSURE) {
                if (x->key >= 0) {
                    ev.type = HEXTER_EV_KEY_PRESSURE;
                    ev.a = (uint8_t)x->key;
                    ev.b = vel7(x->value);
                } else {
                    ev.type = HEXTER_EV_CHANNEL_PRESSURE;
                    ev.a = vel7(x->value);
                }
                h->events[ne++] = ev;
            }
            break;
          }
          case CLAP_EVENT_PARAM_VALUE: {
            const clap_event_param_value_t *p = (const clap_event_param_value_t *)hdr;
            switch (p->param_id) {
              case P_TUNING: hexter_engine_set_tuning(h->engine, (float)p->value); break;
              case P_VOLUME: hexter_engine_set_volume(h->engine, (float)p->value); break;
              case P_POLYPHONY:
                ev.type = HEXTER_EV_POLYPHONY; ev.value = (int32_t)lrint(p->value);
                h->events[ne++] = ev;
                break;
              case P_MONO_MODE:
                ev.type = HEXTER_EV_MONO_MODE; ev.value = (int32_t)lrint(p->value);
                h->events[ne++] = ev;
                break;
              case P_PROGRAM:
                ev.type = HEXTER_EV_PROGRAM_CHANGE;
                ev.a = (uint8_t)(lrint(p->value) < 0 ? 0 : (lrint(p->value) > 127 ? 127 : lrint(p->value)));
                h->events[ne++] = ev;
                break;
              default: break;
            }
            break;
          }
          case CLAP_EVENT_MIDI: {
            const clap_event_midi_t *m = (const clap_event_midi_t *)hdr;
            if (hexter_event_from_midi(m->data, midi_length(m->data[0]), t, &ev))
                h->events[ne++] = ev;
            break;
          }
          case CLAP_EVENT_MIDI_SYSEX: {
            const clap_event_midi_sysex_t *s = (const clap_event_midi_sysex_t *)hdr;
            if (s->buffer && s->size >= 2 && hexter_event_from_midi(s->buffer, s->size, t, &ev))
                h->events[ne++] = ev;
            break;
          }
          default:
            break;
        }
    }

    hexter_engine_render(h->engine, h->mono, nframes, h->events, ne);

    if (process->audio_outputs_count >= 1) {
        const clap_audio_buffer_t *out = &process->audio_outputs[0];
        uint32_t c;
        for (c = 0; c < out->channel_count; c++) {
            if (out->data32 && out->data32[c])
                memcpy(out->data32[c], h->mono, nframes * sizeof(float));
            else if (out->data64 && out->data64[c]) {
                uint32_t k;
                for (k = 0; k < nframes; k++) out->data64[c][k] = h->mono[k];
            }
        }
    }

    /* keep the host's view of the program parameter in sync with MIDI
     * program changes and sysex */
    program_now = hexter_engine_get_program(h->engine);
    if (program_now != h->last_program && process->out_events) {
        clap_event_param_value_t pv;
        memset(&pv, 0, sizeof(pv));
        pv.header.size = sizeof(pv);
        pv.header.time = nframes ? nframes - 1 : 0;
        pv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        pv.header.type = CLAP_EVENT_PARAM_VALUE;
        pv.param_id = P_PROGRAM;
        pv.note_id = -1;
        pv.port_index = -1;
        pv.channel = -1;
        pv.key = -1;
        pv.value = program_now;
        process->out_events->try_push(process->out_events, &pv.header);
        h->last_program = program_now;
    }

    return CLAP_PROCESS_CONTINUE;
}

static const void *
plugin_get_extension(const clap_plugin_t *plugin, const char *id)
{
    if (!strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &ext_audio_ports;
    if (!strcmp(id, CLAP_EXT_NOTE_PORTS))  return &ext_note_ports;
    if (!strcmp(id, CLAP_EXT_PARAMS))      return &ext_params;
    if (!strcmp(id, CLAP_EXT_STATE))       return &ext_state;
    if (!strcmp(id, CLAP_EXT_PRESET_LOAD) || !strcmp(id, CLAP_EXT_PRESET_LOAD_COMPAT))
        return &ext_preset_load;
    return NULL;
}

static void
plugin_on_main_thread(const clap_plugin_t *plugin)
{
    hexter_clap_t *h = (hexter_clap_t *)plugin->plugin_data;

    if (h->rescan_requested) {
        h->rescan_requested = false;
        if (h->host_params)
            h->host_params->rescan(h->host, CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);
    }
    if (h->dirty_requested) {
        h->dirty_requested = false;
        if (h->host_state) h->host_state->mark_dirty(h->host);
    }
}

static const clap_plugin_t *
create_plugin(const clap_host_t *host)
{
    hexter_clap_t *h = (hexter_clap_t *)calloc(1, sizeof(hexter_clap_t));
    if (!h) return NULL;

    h->host = host;
    h->plugin.desc = &hexter_desc;
    h->plugin.plugin_data = h;
    h->plugin.init = plugin_init;
    h->plugin.destroy = plugin_destroy;
    h->plugin.activate = plugin_activate;
    h->plugin.deactivate = plugin_deactivate;
    h->plugin.start_processing = plugin_start_processing;
    h->plugin.stop_processing = plugin_stop_processing;
    h->plugin.reset = plugin_reset;
    h->plugin.process = plugin_process;
    h->plugin.get_extension = plugin_get_extension;
    h->plugin.on_main_thread = plugin_on_main_thread;
    return &h->plugin;
}

/* ---- factory ---- */

static uint32_t
factory_get_plugin_count(const clap_plugin_factory_t *factory)
{
    return 1;
}

static const clap_plugin_descriptor_t *
factory_get_plugin_descriptor(const clap_plugin_factory_t *factory, uint32_t index)
{
    return index == 0 ? &hexter_desc : NULL;
}

static const clap_plugin_t *
factory_create_plugin(const clap_plugin_factory_t *factory, const clap_host_t *host,
                      const char *plugin_id)
{
    if (!clap_version_is_compatible(host->clap_version)) return NULL;
    if (strcmp(plugin_id, HEXTER_CLAP_ID)) return NULL;
    return create_plugin(host);
}

static const clap_plugin_factory_t hexter_factory = {
    .get_plugin_count      = factory_get_plugin_count,
    .get_plugin_descriptor = factory_get_plugin_descriptor,
    .create_plugin         = factory_create_plugin,
};

/* ---- entry ---- */

static bool
entry_init(const char *plugin_path)
{
    return true;
}

static void
entry_deinit(void)
{
}

static const void *
entry_get_factory(const char *factory_id)
{
    if (!strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID)) return &hexter_factory;
    return NULL;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init         = entry_init,
    .deinit       = entry_deinit,
    .get_factory  = entry_get_factory,
};
