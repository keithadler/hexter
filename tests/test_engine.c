/* hexter engine tests
 *
 * Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.
 *
 * Run with the path to the directory holding the .dx7 banks (extra/).
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "hexter_engine.h"
#include "dx7_voice.h"
#include "dx7_bank.h"
#include "dx7_voice_data.h"

static int failures = 0, checks = 0;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *bank_dir = ".";

static char *
bank_path(const char *name)
{
    static char buf[1024];
    snprintf(buf, sizeof(buf), "%s/%s", bank_dir, name);
    return buf;
}

static void
render_seconds(hexter_engine_t *e, float *out, uint32_t frames, const hexter_event_t *evs, uint32_t nev)
{
    uint32_t done = 0;
    uint32_t next = 0;
    while (done < frames) {
        uint32_t n = frames - done < 256 ? frames - done : 256;
        hexter_event_t local[64];
        uint32_t nl = 0;
        while (next < nev && nl < 64 && evs[next].frame < done + n) {
            local[nl] = evs[next];
            local[nl].frame = evs[next].frame < done ? 0 : evs[next].frame - done;
            nl++;
            next++;
        }
        hexter_engine_render(e, out + done, n, local, nl);
        done += n;
    }
}

static double
rms(const float *x, uint32_t n)
{
    double s = 0.0;
    uint32_t i;
    for (i = 0; i < n; i++) s += (double)x[i] * x[i];
    return sqrt(s / (n ? n : 1));
}

static int
zero_crossings(const float *x, uint32_t n)
{
    int c = 0;
    uint32_t i;
    for (i = 1; i < n; i++)
        if ((x[i - 1] < 0.0f && x[i] >= 0.0f) || (x[i - 1] >= 0.0f && x[i] < 0.0f)) c++;
    return c;
}

/* ---- bank loading ---- */

static void
test_bank_files(void)
{
    struct { const char *name; int expect; } files[] = {
        { "dx7_roms.dx7", 128 },
        { "tx7_roms.dx7", 64 },
        { "default_patches.dx7", 71 },
        { "fb01_roms_converted_12.dx7", 96 },
        { "fb01_roms_converted_34.dx7", 96 },
        { "fb01_roms_converted_5.dx7", 48 },
    };
    size_t i;

    for (i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        dx7_patch_t patches[128];
        char *err = NULL;
        int n = dx7_patchbank_load(bank_path(files[i].name), patches, 128, &err);
        CHECK(n == files[i].expect, "%s: loaded %d patches, expected %d (%s)",
              files[i].name, n, files[i].expect, err ? err : "");
        free(err);
        if (n > 0) {
            int p, bad = 0;
            for (p = 0; p < n; p++) {
                uint8_t unpacked[155], repacked[128], reunpacked[155];
                dx7_patch_unpack(patches, (uint8_t)p, unpacked);
                dx7_patch_pack(unpacked, (dx7_patch_t *)repacked, 0);
                dx7_patch_unpack((dx7_patch_t *)repacked, 0, reunpacked);
                if (memcmp(unpacked, reunpacked, 155)) bad++;
            }
            CHECK(bad == 0, "%s: %d patches do not survive unpack/pack/unpack", files[i].name, bad);
        }
    }

    /* the ROM bank's first voice is the famous one */
    {
        hexter_engine_t *e = hexter_engine_new(48000.0f);
        char name[11];
        char *err = NULL;
        int n = hexter_engine_load_bank_file(e, bank_path("dx7_roms.dx7"), 0, &err);
        CHECK(n == 128, "engine loaded %d ROM patches", n);
        hexter_engine_get_program_name(e, 0, name);
        CHECK(!strcmp(name, "BRASS   1 "), "ROM 1A program 1 is '%s'", name);
        hexter_engine_get_program_name(e, 10, name);
        CHECK(!strcmp(name, "E.PIANO 1 "), "ROM 1A program 11 is '%s'", name);
        free(err);

        /* memory loader agrees with the file loader */
        {
            FILE *fp = fopen(bank_path("dx7_roms.dx7"), "rb");
            uint8_t *data;
            long size;
            hexter_engine_t *m = hexter_engine_new(48000.0f);
            uint8_t b1[128 * 128], b2[128 * 128];
            CHECK(fp != NULL, "open ROM file");
            fseek(fp, 0, SEEK_END); size = ftell(fp); fseek(fp, 0, SEEK_SET);
            data = (uint8_t *)malloc(size);
            CHECK(fread(data, 1, size, fp) == (size_t)size, "read ROM file");
            fclose(fp);
            n = hexter_engine_load_bank_memory(m, data, size, "dx7_roms.dx7", 0, &err);
            CHECK(n == 128, "memory loader loaded %d", n);
            hexter_engine_get_bank(e, b1);
            hexter_engine_get_bank(m, b2);
            CHECK(!memcmp(b1, b2, sizeof(b1)), "file and memory loaders agree");
            free(data);
            free(err);
            hexter_engine_free(m);
        }

        /* loading at an offset */
        n = hexter_engine_load_bank_file(e, bank_path("tx7_roms.dx7"), 100, &err);
        CHECK(n == 28, "bank load at 100 truncates to %d", n);
        free(err);

        /* garbage */
        {
            uint8_t junk[200];
            memset(junk, 0x55, sizeof(junk));
            n = hexter_engine_load_bank_memory(e, junk, 50, NULL, 0, &err);
            CHECK(n == 0 && err != NULL, "too-small data rejected: %s", err ? err : "(no message)");
            free(err); err = NULL;
            n = hexter_engine_load_bank_file(e, bank_path("does-not-exist.syx"), 0, &err);
            CHECK(n == 0 && err != NULL, "missing file rejected: %s", err ? err : "(no message)");
            free(err);
        }
        hexter_engine_free(e);
    }
}

/* ---- MIDI parsing ---- */

static void
test_midi_parse(void)
{
    hexter_event_t ev;
    uint8_t m[8];

    m[0] = 0x93; m[1] = 60; m[2] = 100;
    CHECK(hexter_event_from_midi(m, 3, 7, &ev) && ev.type == HEXTER_EV_NOTE_ON && ev.a == 60 && ev.b == 100 && ev.frame == 7, "note on on channel 4");
    m[0] = 0x80; m[1] = 60; m[2] = 40;
    CHECK(hexter_event_from_midi(m, 3, 0, &ev) && ev.type == HEXTER_EV_NOTE_OFF && ev.b == 40, "note off");
    m[0] = 0xB0; m[1] = 1; m[2] = 127;
    CHECK(hexter_event_from_midi(m, 3, 0, &ev) && ev.type == HEXTER_EV_CONTROL_CHANGE && ev.a == 1 && ev.b == 127, "cc");
    m[0] = 0xC0; m[1] = 5;
    CHECK(hexter_event_from_midi(m, 2, 0, &ev) && ev.type == HEXTER_EV_PROGRAM_CHANGE && ev.a == 5, "program change");
    m[0] = 0xD0; m[1] = 99;
    CHECK(hexter_event_from_midi(m, 2, 0, &ev) && ev.type == HEXTER_EV_CHANNEL_PRESSURE && ev.a == 99, "channel pressure");
    m[0] = 0xE0; m[1] = 0x00; m[2] = 0x40;
    CHECK(hexter_event_from_midi(m, 3, 0, &ev) && ev.type == HEXTER_EV_PITCH_BEND && ev.value == 0, "pitch bend center");
    m[0] = 0xE0; m[1] = 0x7F; m[2] = 0x7F;
    CHECK(hexter_event_from_midi(m, 3, 0, &ev) && ev.value == 8191, "pitch bend max");
    m[0] = 0xE0; m[1] = 0x00; m[2] = 0x00;
    CHECK(hexter_event_from_midi(m, 3, 0, &ev) && ev.value == -8192, "pitch bend min");
    m[0] = 0xF0; m[1] = 0x43; m[2] = 0xF7;
    CHECK(hexter_event_from_midi(m, 3, 0, &ev) && ev.type == HEXTER_EV_SYSEX && ev.size == 3, "sysex");
    m[0] = 0xF8;
    CHECK(!hexter_event_from_midi(m, 1, 0, &ev), "clock is ignored");
    CHECK(!hexter_event_from_midi(m, 0, 0, &ev), "empty is ignored");
}

/* ---- rendering ---- */

static void
test_render_sine(void)
{
    const uint32_t rate = 48000;
    hexter_engine_t *e = hexter_engine_new((float)rate);
    float *out = (float *)calloc(rate * 6, sizeof(float));
    hexter_event_t evs[2];
    int zc;
    double r;

    /* the init voice is a single sine carrier (algorithm 1, only OP1 sounding) */
    hexter_engine_init_bank(e);
    memset(evs, 0, sizeof(evs));
    evs[0].type = HEXTER_EV_NOTE_ON; evs[0].a = 69; evs[0].b = 100; evs[0].frame = 0;
    evs[1].type = HEXTER_EV_NOTE_OFF; evs[1].a = 69; evs[1].b = 64; evs[1].frame = rate * 2;
    render_seconds(e, out, rate * 6, evs, 2);

    r = rms(out + rate, rate);
    CHECK(r > 0.01 && r < 1.0, "sine second 2 rms %.4f", r);
    zc = zero_crossings(out + rate, rate);
    CHECK(zc >= 870 && zc <= 890, "A4 at 440 Hz gives %d zero crossings per second (expect 880)", zc);
    r = rms(out + rate * 5, rate);
    CHECK(r < 1e-4, "silent 3 s after release: rms %.6f", r);
    CHECK(hexter_engine_get_active_voices(e) == 0, "no active voices after release, have %d",
          hexter_engine_get_active_voices(e));

    /* retune and check the pitch follows */
    hexter_engine_set_tuning(e, 466.2f);
    memset(out, 0, rate * 6 * sizeof(float));
    render_seconds(e, out, rate * 4, evs, 2);
    zc = zero_crossings(out + rate, rate);
    CHECK(zc >= 922 && zc <= 943, "A4 at 466.2 Hz gives %d zero crossings (expect 932)", zc);

    /* volume parameter changes level */
    hexter_engine_set_tuning(e, 440.0f);
    hexter_engine_set_volume(e, -20.0f);
    memset(out, 0, rate * 6 * sizeof(float));
    render_seconds(e, out, rate * 4, evs, 2);
    {
        double quiet = rms(out + rate, rate);
        hexter_engine_set_volume(e, 0.0f);
        memset(out, 0, rate * 6 * sizeof(float));
        render_seconds(e, out, rate * 4, evs, 2);
        r = rms(out + rate, rate);
        CHECK(quiet < r * 0.2 && quiet > r * 0.05, "-20 dB is about a tenth: %.4f vs %.4f", quiet, r);
    }

    free(out);
    hexter_engine_free(e);
}

static void
test_determinism_and_rom(void)
{
    const uint32_t rate = 44100;
    hexter_engine_t *a = hexter_engine_new((float)rate), *b = hexter_engine_new((float)rate);
    float *oa = (float *)calloc(rate * 3, sizeof(float)), *ob = (float *)calloc(rate * 3, sizeof(float));
    hexter_event_t evs[8];
    int i, n = 0;
    char *err = NULL;

    CHECK(hexter_engine_load_bank_file(a, bank_path("dx7_roms.dx7"), 0, &err) == 128, "load a");
    CHECK(hexter_engine_load_bank_file(b, bank_path("dx7_roms.dx7"), 0, &err) == 128, "load b");
    free(err);

    memset(evs, 0, sizeof(evs));
    evs[n].type = HEXTER_EV_PROGRAM_CHANGE; evs[n].a = 10; evs[n].frame = 0; n++;        /* E.PIANO 1 */
    evs[n].type = HEXTER_EV_NOTE_ON; evs[n].a = 60; evs[n].b = 90; evs[n].frame = 0; n++;
    evs[n].type = HEXTER_EV_NOTE_ON; evs[n].a = 64; evs[n].b = 90; evs[n].frame = 100; n++;
    evs[n].type = HEXTER_EV_NOTE_ON; evs[n].a = 67; evs[n].b = 90; evs[n].frame = 200; n++;
    evs[n].type = HEXTER_EV_CONTROL_CHANGE; evs[n].a = 1; evs[n].b = 100; evs[n].frame = 5000; n++;
    evs[n].type = HEXTER_EV_PITCH_BEND; evs[n].value = 4000; evs[n].frame = 20000; n++;
    evs[n].type = HEXTER_EV_NOTE_OFF; evs[n].a = 60; evs[n].b = 64; evs[n].frame = rate; n++;
    evs[n].type = HEXTER_EV_NOTE_OFF; evs[n].a = 64; evs[n].b = 64; evs[n].frame = rate; n++;

    render_seconds(a, oa, rate * 3, evs, n);
    render_seconds(b, ob, rate * 3, evs, n);
    CHECK(!memcmp(oa, ob, rate * 3 * sizeof(float)), "two engines render bit-identical output");
    CHECK(rms(oa, rate) > 0.01, "E.PIANO 1 chord is audible: rms %.4f", rms(oa, rate));
    CHECK(hexter_engine_get_program(a) == 10, "program change applied: %d", hexter_engine_get_program(a));
    for (i = 0; i < (int)(rate * 3); i++) {
        if (!(oa[i] <= 1.5f && oa[i] >= -1.5f)) { CHECK(0, "sample %d out of range: %f", i, oa[i]); break; }
    }

    /* every ROM program plays without blowing up */
    {
        int bad = 0, silent = 0;
        for (i = 0; i < 128; i++) {
            hexter_event_t ev2[2];
            memset(ev2, 0, sizeof(ev2));
            hexter_engine_reset(a);
            hexter_engine_select_program(a, i);
            ev2[0].type = HEXTER_EV_NOTE_ON; ev2[0].a = 60; ev2[0].b = 100; ev2[0].frame = 0;
            ev2[1].type = HEXTER_EV_NOTE_OFF; ev2[1].a = 60; ev2[1].b = 64; ev2[1].frame = rate / 2;
            render_seconds(a, oa, rate, ev2, 2);
            {
                double r = rms(oa, rate / 2);
                int k;
                if (r < 1e-5) silent++;
                for (k = 0; k < (int)rate; k++) if (!(oa[k] == oa[k])) { bad++; break; }
            }
        }
        CHECK(bad == 0, "%d ROM programs produced NaN", bad);
        CHECK(silent <= 2, "%d ROM programs were silent on middle C", silent);
    }

    free(oa); free(ob);
    hexter_engine_free(a); hexter_engine_free(b);
}

/* ---- sysex ---- */

static void
test_sysex(void)
{
    const uint32_t rate = 48000;
    hexter_engine_t *e = hexter_engine_new((float)rate);
    dx7_patch_t rom[128];
    uint8_t bulk[4104], single[163], param[7];
    uint8_t unpacked[155], cur[155];
    hexter_event_t ev;
    float out[256];
    char name[11];
    char *err = NULL;

    CHECK(dx7_patchbank_load(bank_path("dx7_roms.dx7"), rom, 128, &err) == 128, "load ROM for sysex");
    free(err);

    /* bulk dump of ROM 1A into a fresh engine */
    bulk[0] = 0xF0; bulk[1] = 0x43; bulk[2] = 0x00; bulk[3] = 0x09; bulk[4] = 0x20; bulk[5] = 0x00;
    memcpy(bulk + 6, rom, 4096);
    bulk[4102] = (uint8_t)dx7_bulk_dump_checksum(bulk + 6, 4096);
    bulk[4103] = 0xF7;
    CHECK(hexter_event_from_midi(bulk, sizeof(bulk), 0, &ev), "bulk dump parses as sysex");
    hexter_engine_render(e, out, 256, &ev, 1);
    hexter_engine_get_program_name(e, 0, name);
    CHECK(!strcmp(name, "BRASS   1 "), "bulk dump loaded program 1 = '%s'", name);
    hexter_engine_get_program_name(e, 31, name);
    CHECK(!strcmp(name, "TAKE OFF  "), "bulk dump loaded program 32 = '%s'", name);
    hexter_engine_get_current_patch(e, cur);
    dx7_patch_unpack(rom, 0, unpacked);
    CHECK(!memcmp(cur, unpacked, 155), "current patch refreshed from the new bank");

    /* corrupt checksum is rejected */
    hexter_engine_init_bank(e);
    bulk[4102] ^= 0x01;
    hexter_event_from_midi(bulk, sizeof(bulk), 0, &ev);
    hexter_engine_render(e, out, 256, &ev, 1);
    hexter_engine_get_program_name(e, 0, name);
    CHECK(strcmp(name, "BRASS   1 ") != 0, "bad checksum rejected, program 1 = '%s'", name);
    bulk[4102] ^= 0x01;

    /* single voice dump goes to the edit buffer */
    dx7_patch_unpack(rom, 10, unpacked);   /* E.PIANO 1 */
    single[0] = 0xF0; single[1] = 0x43; single[2] = 0x00; single[3] = 0x00; single[4] = 0x01; single[5] = 0x1B;
    memcpy(single + 6, unpacked, 155);
    single[161] = (uint8_t)dx7_bulk_dump_checksum(single + 6, 155);
    single[162] = 0xF7;
    hexter_event_from_midi(single, sizeof(single), 0, &ev);
    hexter_engine_render(e, out, 256, &ev, 1);
    hexter_engine_get_current_patch(e, cur);
    CHECK(!memcmp(cur, unpacked, 155), "single voice dump became the current patch");
    CHECK(!memcmp(cur + 145, "E.PIANO 1 ", 10), "current patch name is E.PIANO 1");

    /* it survives reselecting the same program, is dropped by another */
    hexter_engine_select_program(e, 0);
    hexter_engine_get_current_patch(e, cur);
    CHECK(!memcmp(cur, unpacked, 155), "edit buffer kept on reselect of the same program");
    hexter_engine_select_program(e, 1);
    hexter_engine_get_current_patch(e, cur);
    CHECK(memcmp(cur, unpacked, 155) != 0, "edit buffer dropped on a different program");
    hexter_engine_select_program(e, 0);

    /* voice parameter change: OP1 output level (index 5*21+16 = 121) to 0 */
    param[0] = 0xF0; param[1] = 0x43; param[2] = 0x10; param[3] = 0x00; param[4] = 121; param[5] = 0; param[6] = 0xF7;
    hexter_event_from_midi(param, 7, 0, &ev);
    hexter_engine_render(e, out, 256, &ev, 1);
    hexter_engine_get_current_patch(e, cur);
    CHECK(cur[121] == 0, "parameter change set OP1 output level to %d", cur[121]);
    /* parameter 134 (algorithm) with high bit via byte 3 */
    param[3] = 0x01; param[4] = 134 - 128; param[5] = 31;
    hexter_event_from_midi(param, 7, 0, &ev);
    hexter_engine_render(e, out, 256, &ev, 1);
    hexter_engine_get_current_patch(e, cur);
    CHECK(cur[134] == 31, "parameter change set algorithm to %d", cur[134]);
    /* out-of-range value is clamped */
    param[3] = 0x01; param[4] = 134 - 128; param[5] = 100;
    hexter_event_from_midi(param, 7, 0, &ev);
    hexter_engine_render(e, out, 256, &ev, 1);
    hexter_engine_get_current_patch(e, cur);
    CHECK(cur[134] == 31, "algorithm clamped to %d", cur[134]);

    /* function parameter: pitch bend range to 12 */
    {
        uint8_t perf[64];
        param[3] = 0x08; param[4] = 65; param[5] = 12;
        hexter_event_from_midi(param, 7, 0, &ev);
        hexter_engine_render(e, out, 256, &ev, 1);
        hexter_engine_get_performance(e, perf);
        CHECK(perf[3] == 12, "function change set pitch bend range to %d", perf[3]);
    }

    /* store the edit buffer as program 5 */
    hexter_engine_store_current_patch(e, 5);
    hexter_engine_get_program_name(e, 5, name);
    CHECK(!strcmp(name, "E.PIANO 1 "), "stored edit buffer as program 6 = '%s'", name);

    hexter_engine_free(e);
}

/* ---- state ---- */

static void
test_state(void)
{
    hexter_engine_t *a = hexter_engine_new(48000.0f), *b = hexter_engine_new(48000.0f);
    uint8_t state[HEXTER_STATE_SIZE], b1[128 * 128], b2[128 * 128], p1[64], p2[64], c1[155], c2[155];
    float oa[1024], ob[1024];
    hexter_event_t ev;
    char *err = NULL;

    CHECK(hexter_engine_load_bank_file(a, bank_path("tx7_roms.dx7"), 0, &err) == 64, "load tx7");
    free(err);
    hexter_engine_set_tuning(a, 432.0f);
    hexter_engine_set_volume(a, -6.0f);
    hexter_engine_set_polyphony(a, 23);
    hexter_engine_set_mono_mode(a, HEXTER_MONO_ONCE);
    hexter_engine_select_program(a, 17);
    hexter_engine_set_voice_parameter(a, 121, 42);

    CHECK(hexter_engine_state_save(a, state, sizeof(state)) == HEXTER_STATE_SIZE, "state save size");
    CHECK(hexter_engine_state_save(a, state, 10) == 0, "state save refuses a small buffer");
    CHECK(hexter_engine_state_load(b, state, sizeof(state)), "state load");

    CHECK(fabs(hexter_engine_get_tuning(b) - 432.0f) < 1e-6, "tuning restored %f", hexter_engine_get_tuning(b));
    CHECK(fabs(hexter_engine_get_volume(b) + 6.0f) < 1e-6, "volume restored %f", hexter_engine_get_volume(b));
    CHECK(hexter_engine_get_polyphony(b) == 23, "polyphony restored %d", hexter_engine_get_polyphony(b));
    CHECK(hexter_engine_get_mono_mode(b) == HEXTER_MONO_ONCE, "mono mode restored %d", hexter_engine_get_mono_mode(b));
    CHECK(hexter_engine_get_program(b) == 17, "program restored %d", hexter_engine_get_program(b));
    hexter_engine_get_bank(a, b1); hexter_engine_get_bank(b, b2);
    CHECK(!memcmp(b1, b2, sizeof(b1)), "bank restored");
    hexter_engine_get_performance(a, p1); hexter_engine_get_performance(b, p2);
    CHECK(!memcmp(p1, p2, 64), "performance restored");
    hexter_engine_get_current_patch(a, c1); hexter_engine_get_current_patch(b, c2);
    CHECK(!memcmp(c1, c2, 155) && c2[121] == 42, "edit buffer restored (OP1 level %d)", c2[121]);

    memset(&ev, 0, sizeof(ev));
    ev.type = HEXTER_EV_NOTE_ON; ev.a = 60; ev.b = 100;
    hexter_engine_render(a, oa, 1024, &ev, 1);
    hexter_engine_render(b, ob, 1024, &ev, 1);
    CHECK(!memcmp(oa, ob, sizeof(oa)), "restored engine renders identically");

    /* rejects junk */
    state[0] = 'X';
    CHECK(!hexter_engine_state_load(b, state, sizeof(state)), "bad magic rejected");
    CHECK(!hexter_engine_state_load(b, state, 100), "short state rejected");

    hexter_engine_free(a); hexter_engine_free(b);
}

/* ---- voice management ---- */

static void
test_voices(void)
{
    hexter_engine_t *e = hexter_engine_new(48000.0f);
    hexter_event_t evs[70];
    float out[512];
    int i, n = 0;

    hexter_engine_set_polyphony(e, 16);
    memset(evs, 0, sizeof(evs));
    for (i = 0; i < 64; i++) {
        evs[n].type = HEXTER_EV_NOTE_ON; evs[n].a = (uint8_t)(30 + i); evs[n].b = 100; evs[n].frame = (uint32_t)i; n++;
    }
    hexter_engine_render(e, out, 512, evs, n);
    CHECK(hexter_engine_get_active_voices(e) == 16, "64 notes at polyphony 16 leave %d voices", hexter_engine_get_active_voices(e));

    n = 0;
    evs[n].type = HEXTER_EV_POLYPHONY; evs[n].value = 4; evs[n].frame = 0; n++;
    hexter_engine_render(e, out, 512, evs, n);
    CHECK(hexter_engine_get_active_voices(e) <= 4, "polyphony 4 event leaves %d voices", hexter_engine_get_active_voices(e));
    CHECK(hexter_engine_get_polyphony(e) == 4, "polyphony getter %d", hexter_engine_get_polyphony(e));

    n = 0;
    evs[n].type = HEXTER_EV_MONO_MODE; evs[n].value = HEXTER_MONO_ON; evs[n].frame = 0; n++;
    evs[n].type = HEXTER_EV_NOTE_ON; evs[n].a = 60; evs[n].b = 100; evs[n].frame = 10; n++;
    evs[n].type = HEXTER_EV_NOTE_ON; evs[n].a = 62; evs[n].b = 100; evs[n].frame = 20; n++;
    evs[n].type = HEXTER_EV_NOTE_ON; evs[n].a = 64; evs[n].b = 100; evs[n].frame = 30; n++;
    hexter_engine_render(e, out, 512, evs, n);
    CHECK(hexter_engine_get_active_voices(e) == 1, "mono mode leaves %d voice", hexter_engine_get_active_voices(e));
    n = 0;
    evs[n].type = HEXTER_EV_NOTE_OFF; evs[n].a = 64; evs[n].b = 64; evs[n].frame = 0; n++;
    hexter_engine_render(e, out, 512, evs, n);
    CHECK(hexter_engine_get_active_voices(e) == 1, "mono returns to a held key: %d voice", hexter_engine_get_active_voices(e));

    /* all notes off, all sound off, sustain */
    n = 0;
    evs[n].type = HEXTER_EV_MONO_MODE; evs[n].value = HEXTER_MONO_OFF; evs[n].frame = 0; n++;
    evs[n].type = HEXTER_EV_CONTROL_CHANGE; evs[n].a = 64; evs[n].b = 127; evs[n].frame = 0; n++;   /* sustain on */
    evs[n].type = HEXTER_EV_NOTE_ON; evs[n].a = 60; evs[n].b = 100; evs[n].frame = 1; n++;
    evs[n].type = HEXTER_EV_NOTE_OFF; evs[n].a = 60; evs[n].b = 64; evs[n].frame = 2; n++;
    hexter_engine_render(e, out, 512, evs, n);
    CHECK(hexter_engine_get_active_voices(e) >= 1, "sustain holds the voice: %d", hexter_engine_get_active_voices(e));
    n = 0;
    evs[n].type = HEXTER_EV_CONTROL_CHANGE; evs[n].a = 120; evs[n].b = 0; evs[n].frame = 0; n++;   /* all sound off */
    hexter_engine_render(e, out, 512, evs, n);
    CHECK(hexter_engine_get_active_voices(e) == 0, "all sound off leaves %d", hexter_engine_get_active_voices(e));

    /* events at the very end of a block are still applied */
    n = 0;
    evs[n].type = HEXTER_EV_NOTE_ON; evs[n].a = 60; evs[n].b = 100; evs[n].frame = 512; n++;
    hexter_engine_render(e, out, 512, evs, n);
    CHECK(hexter_engine_get_active_voices(e) >= 1, "event at frame == nframes applied");

    /* zero-length render is harmless */
    hexter_engine_render(e, out, 0, NULL, 0);

    /* bad values are clamped */
    CHECK(hexter_engine_set_polyphony(e, 500) == 64, "polyphony clamps high");
    CHECK(hexter_engine_set_polyphony(e, 0) == 1, "polyphony clamps low");
    CHECK(hexter_engine_set_mono_mode(e, 9) == 3, "mono mode clamps");
    hexter_engine_set_tuning(e, 100.0f);
    CHECK(fabs(hexter_engine_get_tuning(e) - HEXTER_ENGINE_TUNING_MIN) < 1e-6, "tuning clamps");
    hexter_engine_select_program(e, 999);
    CHECK(hexter_engine_get_program(e) == 0, "bad program ignored");

    hexter_engine_free(e);
    CHECK(hexter_engine_new(10.0f) == NULL, "absurd sample rate refused");
}

/* ---- awkward sample rates (clap-validator's list, plus extremes) ---- */

static void
test_sample_rates(void)
{
    const float rates[] = { 1000.0f, 1234.5678f, 8000.0f, 12345.678f, 22050.0f, 44100.0f,
                            45678.901f, 48000.0f, 88200.0f, 96000.0f, 123456.78f, 192000.0f,
                            384000.0f, 768000.0f };
    size_t r;

    for (r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
        hexter_engine_t *e = hexter_engine_new(rates[r]);
        hexter_event_t evs[4];
        float out[512];
        int i, k, nan = 0;
        double energy = 0.0;
        char *err = NULL;

        CHECK(e != NULL, "engine at %g Hz", rates[r]);
        if (!e) continue;
        hexter_engine_load_bank_file(e, bank_path("dx7_roms.dx7"), 0, &err);
        free(err);
        memset(evs, 0, sizeof(evs));
        for (k = 0; k < 128; k += 7) {
            hexter_engine_select_program(e, k);
            evs[0].type = HEXTER_EV_NOTE_ON; evs[0].a = (uint8_t)(24 + k / 2); evs[0].b = 1 + k; evs[0].frame = 0;
            evs[1].type = HEXTER_EV_NOTE_ON; evs[1].a = 127; evs[1].b = 127; evs[1].frame = 3;
            evs[2].type = HEXTER_EV_CONTROL_CHANGE; evs[2].a = 1; evs[2].b = 127; evs[2].frame = 5;
            evs[3].type = HEXTER_EV_PITCH_BEND; evs[3].value = -8192; evs[3].frame = 7;
            hexter_engine_render(e, out, 512, evs, 4);
            for (i = 0; i < 30; i++) {
                int j;
                hexter_engine_render(e, out, 512, NULL, 0);
                for (j = 0; j < 512; j++) {
                    if (out[j] != out[j]) nan++;
                    energy += (double)out[j] * out[j];
                }
            }
            evs[0].type = HEXTER_EV_NOTE_OFF; evs[1].type = HEXTER_EV_NOTE_OFF;
            hexter_engine_render(e, out, 512, evs, 2);
            for (i = 0; i < 10; i++) hexter_engine_render(e, out, 512, NULL, 0);
        }
        CHECK(nan == 0, "%g Hz: %d NaN samples", rates[r], nan);
        CHECK(energy > 0.0, "%g Hz: produced sound", rates[r]);
        hexter_engine_free(e);
    }
}

int
main(int argc, char **argv)
{
    if (argc > 1) bank_dir = argv[1];

    test_bank_files();
    test_midi_parse();
    test_render_sine();
    test_determinism_and_rom();
    test_sysex();
    test_state();
    test_voices();
    test_sample_rates();

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
