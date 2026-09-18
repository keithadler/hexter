/* hexter for iOS - see hexter_bridge.h
 *
 * Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.
 */
#include "hexter_bridge.h"

#include <math.h>
#include <os/lock.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hexter_engine.h"

#define SLOTS       64
#define MAX_EVENTS  256
#define SYSEX_SLOTS 8

typedef struct {
    uint32_t len;
    uint8_t  data[HEXTER_BRIDGE_MAX_MESSAGE];
} slot_t;

struct hexter_bridge {
    hexter_engine_t *engine;
    /* single consumer (the audio thread); producers serialize on `producer` */
    slot_t          slots[SLOTS];
    atomic_int      read, write;
    os_unfair_lock  producer;
    hexter_event_t  events[MAX_EVENTS];
    uint8_t         sysex[SYSEX_SLOTS][HEXTER_BRIDGE_MAX_MESSAGE];
    _Atomic float   peak;
};

hexter_bridge_t *hexter_bridge_new(float sample_rate)
{
    hexter_bridge_t *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    b->engine = hexter_engine_new(sample_rate);
    if (!b->engine) { free(b); return NULL; }
    b->producer = (os_unfair_lock)OS_UNFAIR_LOCK_INIT;
    return b;
}

void hexter_bridge_free(hexter_bridge_t *b)
{
    if (!b) return;
    hexter_engine_free(b->engine);
    free(b);
}

bool hexter_bridge_midi(hexter_bridge_t *b, const uint8_t *msg, size_t len)
{
    if (!b || !msg || len == 0 || len > HEXTER_BRIDGE_MAX_MESSAGE) return false;
    os_unfair_lock_lock(&b->producer);
    int w = atomic_load_explicit(&b->write, memory_order_relaxed);
    int next = (w + 1) % SLOTS;
    bool ok = next != atomic_load_explicit(&b->read, memory_order_acquire);   /* else full */
    if (ok) {
        b->slots[w].len = (uint32_t)len;
        memcpy(b->slots[w].data, msg, len);
        atomic_store_explicit(&b->write, next, memory_order_release);
    }
    os_unfair_lock_unlock(&b->producer);
    return ok;
}

void hexter_bridge_render(hexter_bridge_t *b, float *out, uint32_t frames)
{
    uint32_t n = 0;
    int r = atomic_load_explicit(&b->read, memory_order_relaxed);
    while (n < MAX_EVENTS && r != atomic_load_explicit(&b->write, memory_order_acquire)) {
        slot_t *s = &b->slots[r];
        if (hexter_event_from_midi(s->data, s->len, 0, &b->events[n]) && b->events[n].type != HEXTER_EV_NONE) {
            if (b->events[n].type == HEXTER_EV_SYSEX) {        /* keep the bytes alive through render() */
                memcpy(b->sysex[n % SYSEX_SLOTS], s->data, s->len);
                b->events[n].data = b->sysex[n % SYSEX_SLOTS];
            }
            n++;
        }
        r = (r + 1) % SLOTS;
        atomic_store_explicit(&b->read, r, memory_order_release);
    }
    hexter_engine_render(b->engine, out, frames, b->events, n);

    float p = 0.0f;
    for (uint32_t i = 0; i < frames; i++) {
        float a = fabsf(out[i]);
        if (a > p) p = a;
    }
    float cur = atomic_load_explicit(&b->peak, memory_order_relaxed);
    if (p > cur) atomic_store_explicit(&b->peak, p, memory_order_relaxed);
}

float hexter_bridge_peak(hexter_bridge_t *b)
{
    return b ? atomic_exchange_explicit(&b->peak, 0.0f, memory_order_relaxed) : 0.0f;
}

int hexter_bridge_load_bank(hexter_bridge_t *b, const uint8_t *data, size_t size,
                            const char *name_hint, char *err, size_t err_size)
{
    if (!b) return -1;
    char *msg = NULL;
    int n = hexter_engine_load_bank_memory(b->engine, data, size, name_hint ? name_hint : "bank.syx", 0, &msg);
    if (n <= 0) {
        if (err && err_size) snprintf(err, err_size, "%s", msg ? msg : "unknown error");
        free(msg);
        return n < 0 ? n : -1;
    }
    free(msg);
    hexter_engine_select_program(b->engine, 0);
    return n;
}

void hexter_bridge_program_name(hexter_bridge_t *b, int program, char *name12)
{
    name12[0] = 0;
    if (b) hexter_engine_get_program_name(b->engine, program, name12);
}

void hexter_bridge_select_program(hexter_bridge_t *b, int program)
{
    if (b) hexter_engine_select_program(b->engine, program);
}

int hexter_bridge_program(hexter_bridge_t *b)
{
    return b ? hexter_engine_get_program(b->engine) : 0;
}

void hexter_bridge_set_volume(hexter_bridge_t *b, float db)
{
    if (b) hexter_engine_set_volume(b->engine, db);
}

int hexter_bridge_set_polyphony(hexter_bridge_t *b, int voices)
{
    return b ? hexter_engine_set_polyphony(b->engine, voices) : 0;
}

int hexter_bridge_active_voices(hexter_bridge_t *b)
{
    return b ? hexter_engine_get_active_voices(b->engine) : 0;
}
