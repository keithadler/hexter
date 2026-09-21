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
#include "dx7_voice_fb01.h"
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

/* ---- algorithm ---- */

static void
test_algorithm(void)
{
    hexter_engine_t *e1 = hexter_engine_new(44100.0f);
    hexter_engine_t *e2 = hexter_engine_new(44100.0f);
    static float held[11025], changed[11025];
    hexter_event_t note;
    char path[1024];
    uint8_t cur[155];
    int alg0;

    snprintf(path, sizeof(path), "%s/dx7_roms.dx7", bank_dir);
    hexter_engine_load_bank_file(e1, path, 0, NULL);
    hexter_engine_load_bank_file(e2, path, 0, NULL);

    /* the getter agrees with the patch data, and refuses a bad index */
    hexter_engine_get_current_patch(e1, cur);
    alg0 = hexter_engine_get_voice_parameter(e1, HEXTER_VOICE_PARAM_ALGORITHM);
    CHECK(alg0 == cur[HEXTER_VOICE_PARAM_ALGORITHM],
          "getter reads algorithm %d, patch has %d", alg0, cur[HEXTER_VOICE_PARAM_ALGORITHM]);
    CHECK(hexter_engine_get_voice_parameter(e1, 155) == -1, "index past the end is refused");
    CHECK(hexter_engine_get_voice_parameter(e1, -1) == -1, "negative index is refused");

    hexter_engine_set_voice_parameter(e1, HEXTER_VOICE_PARAM_ALGORITHM, 4);
    CHECK(hexter_engine_get_voice_parameter(e1, HEXTER_VOICE_PARAM_ALGORITHM) == 4,
          "algorithm round-trips through the setter");
    hexter_engine_set_voice_parameter(e1, HEXTER_VOICE_PARAM_ALGORITHM, alg0);

    /* both engines: same note, same first half */
    memset(&note, 0, sizeof(note));
    note.type = HEXTER_EV_NOTE_ON; note.a = 60; note.b = 100;
    render_seconds(e1, held, 5512, &note, 1);
    render_seconds(e2, changed, 5512, &note, 1);
    CHECK(!memcmp(held, changed, sizeof(float) * 5512), "same note renders the same in both");

    /* one keeps going, the other has its algorithm moved with the note still down */
    hexter_engine_set_voice_parameter(e2, HEXTER_VOICE_PARAM_ALGORITHM,
                                      alg0 == 31 ? 0 : 31);
    render_seconds(e1, held + 5512, 5513, NULL, 0);
    render_seconds(e2, changed + 5512, 5513, NULL, 0);
    CHECK(memcmp(held + 5512, changed + 5512, sizeof(float) * 5513) != 0,
          "the algorithm change is heard under a held note");

    /* selecting a program brings back that patch's own algorithm */
    hexter_engine_select_program(e2, 7);
    hexter_engine_render(e2, changed, 256, NULL, 0);
    hexter_engine_get_current_patch(e2, cur);
    CHECK(hexter_engine_get_voice_parameter(e2, HEXTER_VOICE_PARAM_ALGORITHM)
              == cur[HEXTER_VOICE_PARAM_ALGORITHM],
          "a program change replaces the edited algorithm");

    hexter_engine_free(e1);
    hexter_engine_free(e2);
}

/* ---- four-operator banks ---- */

/* Build a DX21/DX27/DX100 32-voice dump with known values in it. */
static size_t
build_4op_dump(uint8_t *out, size_t cap, uint8_t format, int waves)
{
    static const char *names[2] = { "FOUR OP 1 ", "FOUR OP 2 " };
    size_t n = 0;
    int v, i;
    unsigned sum = 0;

    if (cap < 6 + 4096 + 2) return 0;
    out[n++] = 0xf0; out[n++] = 0x43; out[n++] = 0x00;
    out[n++] = format;                     /* 0x03 four-operator, 0x04 TX81Z */
    out[n++] = 0x20; out[n++] = 0x00;      /* 4096 bytes follow */

    memset(out + n, 0, 4096);
    for (v = 0; v < 32; v++) {
        uint8_t *p = out + n + v * 128;
        /* operators are stored OP4, OP2, OP3, OP1 */
        static const int at[4] = { 30, 10, 20, 0 };   /* OP1, OP2, OP3, OP4 */
        for (i = 0; i < 4; i++) {
            uint8_t *o = p + at[i];
            o[0] = 31;                      /* attack rate, the maximum */
            o[3] = 10;                      /* release rate */
            o[4] = 15;                      /* decay 1 level, no decay */
            o[7] = (uint8_t)(90 - i * 10);  /* output level, distinct per operator */
            o[8] = 4;                       /* frequency ratio 1.00 */
            o[9] = 3;                       /* detune centered */
        }
        p[40] = (uint8_t)(1 | (5 << 3));    /* algorithm 2 (index 1), feedback 5 */
        p[41] = 33;                         /* LFO speed */
        p[45] = 2;                          /* LFO wave: triangle */
        p[46] = 24;                         /* transpose, centered */
        memcpy(p + 57, names[v & 1], 10);
        p[67] = p[68] = p[69] = 99;         /* pitch envelope at rest */
        p[70] = p[71] = p[72] = 50;
        if (waves) {
            /* the TX81Z extras: an operator waveform in bits 6-4, one byte
             * each, a different shape per operator so the routing shows */
            p[80] = 3 << 4;                 /* OP1 */
            p[76] = 5 << 4;                 /* OP2 */
            p[78] = 1 << 4;                 /* OP3 */
            p[74] = 7 << 4;                 /* OP4 */
        }
    }
    for (i = 0; i < 4096; i++) sum += out[n + i];
    n += 4096;
    out[n++] = (uint8_t)((~sum + 1) & 0x7f);
    out[n++] = 0xf7;
    return n;
}

static void
test_4op_bank(void)
{
    hexter_engine_t *e = hexter_engine_new(44100.0f);
    static uint8_t dump[6 + 4096 + 2];
    uint8_t cur[155];
    char name[11];
    char *err = NULL;
    size_t len;
    int n;

    len = build_4op_dump(dump, sizeof(dump), 0x03, 0);
    CHECK(len == 6 + 4096 + 2, "built a %zu byte four-operator dump", len);

    n = hexter_engine_load_bank_memory(e, dump, len, "dx100.syx", 0, &err);
    CHECK(n == 32, "four-operator dump loaded %d voices (%s)", n, err ? err : "no error");
    free(err); err = NULL;

    /* a TX81Z bank is the same dump with format 0x04 */
    {
        static uint8_t tx[6 + 4096 + 2];
        hexter_engine_t *t = hexter_engine_new(44100.0f);
        size_t tlen = build_4op_dump(tx, sizeof(tx), 0x04, 0);
        char *terr = NULL;
        int tn = hexter_engine_load_bank_memory(t, tx, tlen, "tx81z.syx", 0, &terr);
        CHECK(tn == 32, "TX81Z dump loaded %d voices (%s)", tn, terr ? terr : "no error");
        free(terr);
        hexter_engine_free(t);
    }

    hexter_engine_get_program_name(e, 0, name);
    CHECK(!strcmp(name, "FOUR OP 1 "), "voice 1 is named '%s'", name);
    hexter_engine_get_program_name(e, 1, name);
    CHECK(!strcmp(name, "FOUR OP 2 "), "voice 2 is named '%s'", name);

    hexter_engine_select_program(e, 0);
    hexter_engine_get_current_patch(e, cur);

    /* four-operator algorithm 2 stands in as DX7 algorithm 14, stored as 13 */
    CHECK(cur[134] == 13, "algorithm 2 became DX7 algorithm %d", cur[134] + 1);
    CHECK(cur[135] == 5, "feedback carried across as %d", cur[135]);

    /* For that algorithm the operators land on DX7 4, 5, 3, 6. The unpacked
     * voice stores OP6 first, so operator k starts at (6 - k) * 21. Each
     * four-operator operator was given its own output level, so this checks
     * the routing rather than just that something arrived. */
    CHECK(cur[(6 - 4) * 21 + 16] == 90, "OP1 became DX7 OP4 (level %d)", cur[(6 - 4) * 21 + 16]);
    CHECK(cur[(6 - 5) * 21 + 16] == 80, "OP2 became DX7 OP5 (level %d)", cur[(6 - 5) * 21 + 16]);
    CHECK(cur[(6 - 3) * 21 + 16] == 70, "OP3 became DX7 OP3 (level %d)", cur[(6 - 3) * 21 + 16]);
    CHECK(cur[(6 - 6) * 21 + 16] == 60, "OP4 became DX7 OP6 (level %d)", cur[(6 - 6) * 21 + 16]);
    /* the two operators with nowhere to come from stay silent */
    CHECK(cur[(6 - 1) * 21 + 16] == 0 && cur[(6 - 2) * 21 + 16] == 0,
          "the spare operators are silent");

    /* ratio 1.00 is coarse 1, fine 0; detune 3 centers on the DX7's 7 */
    CHECK(cur[(6 - 4) * 21 + 18] == 1 && cur[(6 - 4) * 21 + 19] == 0,
          "frequency ratio 1.00 became coarse %d fine %d",
          cur[(6 - 4) * 21 + 18], cur[(6 - 4) * 21 + 19]);
    CHECK(cur[(6 - 4) * 21 + 20] == 7, "centered detune became %d", cur[(6 - 4) * 21 + 20]);

    /* the envelope reaches the top and holds there */
    CHECK(cur[(6 - 4) * 21 + 0] == 99, "maximum attack rate became %d", cur[(6 - 4) * 21 + 0]);
    CHECK(cur[(6 - 4) * 21 + 5] == 99, "no-decay sustain became %d", cur[(6 - 4) * 21 + 5]);

    CHECK(cur[142] == 0, "triangle LFO became wave %d", cur[142]);
    CHECK(cur[137] == 33, "LFO speed carried across as %d", cur[137]);

    /* and it makes a sound */
    {
        static float out[4410];
        hexter_event_t note;
        double peak = 0.0;
        int i;

        memset(&note, 0, sizeof(note));
        note.type = HEXTER_EV_NOTE_ON; note.a = 60; note.b = 100;
        render_seconds(e, out, 4410, &note, 1);
        for (i = 0; i < 4410; i++) if (fabs(out[i]) > peak) peak = fabs(out[i]);
        CHECK(peak > 0.01, "a converted voice makes a sound (peak %.4f)", peak);
    }

    hexter_engine_free(e);
}

/* ---- FB-01 banks ---- */

/* write one FB-01 parameter: two bytes, low nibble first */
static void
fb01_put(uint8_t *params, int index, int value)
{
    params[index * 2]     = (uint8_t)(value & 0x0f);
    params[index * 2 + 1] = (uint8_t)((value >> 4) & 0x0f);
}

/* one operator's eight parameters, at their reversed position */
static uint8_t *
fb01_op(uint8_t *params, int op)          /* op is 1 to 4 */
{
    return params + (16 + (4 - op) * 8) * 2;
}

static uint8_t
fb01_sum(const uint8_t *p, int n)
{
    int i, sum = 0;
    for (i = 0; i < n; i++) sum += p[i];
    return (uint8_t)((-sum) & 0x7f);
}

/*
 * Build an FB-01 bank with known values in it. Voice 0 has every operator
 * switched on; voice 1 has OP4 switched off, so the enable bits can be seen
 * to do something.
 */
/*
 * Rewrite a "voice bank x" dump as the "voice bank 0" one: the same 49 packets
 * behind a four-byte header instead of a seven-byte one. Returns the length.
 */
static size_t
fb01_as_bank0(const uint8_t *in, uint8_t *out)
{
    out[0] = 0xf0; out[1] = 0x43; out[2] = 0x00; out[3] = 0x0c;
    /* everything from the bank's own packet onward is identical */
    memcpy(out + 4, in + 7, FB01_BANK_SIZE - 7);
    return FB01_BANK0_SIZE;
}

static void
build_fb01_bank(uint8_t *out, int loud)
{
    static const char *names[2] = { "FB01 V1", "FB01 V2" };
    /* attenuation, so 0 is the loudest and 127 is silence */
    static const int level[4]   = { 24, 48, 72, 96 };
    static const int sustain[4] = { 0, 15, 5, 0 };
    static const int fine[4]    = { 2, 6, 0, 0 };
    static const int coarse[4]  = { 0, 0, 1, 0 };
    int v, i;

    memset(out, 0, FB01_BANK_SIZE);
    out[0] = 0xf0; out[1] = 0x43; out[2] = 0x75;
    out[3] = 0x00; out[4] = 0x00; out[5] = 0x00; out[6] = 0x00;
    out[7] = 0x00; out[8] = 0x40;              /* 64 bank bytes follow */
    out[73] = fb01_sum(out + 9, 64);

    for (v = 0; v < FB01_BANK_VOICES; v++) {
        uint8_t *voice = out + FB01_VOICE_OFFSET + v * FB01_VOICE_STRIDE;
        uint8_t *p = voice + FB01_VOICE_PARAM_OFF;

        voice[0] = 0x01; voice[1] = 0x00;      /* 128 parameter bytes follow */

        for (i = 0; i < 7; i++) fb01_put(p, i, names[v & 1][i]);
        fb01_put(p, 8,  127);                  /* LFO speed, its maximum */
        fb01_put(p, 9,  127);                  /* amplitude mod depth, load bit clear */
        fb01_put(p, 10, 64);                   /* pitch mod depth, sync bit clear */
        /* enable bits 6,5,4,3 are OP1 to OP4 */
        fb01_put(p, 11, (v & 1) ? 0x70 : 0x78);
        fb01_put(p, 12, 1 | (5 << 3));         /* algorithm 2 (index 1), feedback 5 */
        fb01_put(p, 13, 2 | (6 << 4));         /* amplitude mod 2, pitch mod 6 */
        fb01_put(p, 14, 2 << 5);               /* LFO wave: triangle */
        fb01_put(p, 15, 0xfc);                 /* transpose, four semitones down */

        for (i = 1; i <= 4; i++) {
            uint8_t *o = fb01_op(p, i);
            fb01_put(o, 0, loud ? 0 : level[i - 1]);       /* 0 is the loudest */
            fb01_put(o, 1, (7 << 4) | (i == 1 ? 0x80 : 0));   /* level velocity, curve bit */
            fb01_put(o, 2, (i == 1 ? 15 : 0) << 4);           /* level scaling depth */
            fb01_put(o, 3, (fine[i - 1] << 4) | 1 | (i == 1 ? 0x80 : 0)); /* ratio 1 */
            fb01_put(o, 4, (3 << 6) | 31);                    /* rate scaling, attack */
            fb01_put(o, 5, 31);                               /* first decay */
            fb01_put(o, 6, (coarse[i - 1] << 6) | 0);         /* coarse detune, second decay */
            fb01_put(o, 7, ((loud ? 0 : sustain[i - 1]) << 4) | 15);   /* sustain, release */
        }
        voice[130] = fb01_sum(p, 128);
    }
    out[FB01_BANK_SIZE - 1] = 0xf7;
}

static void
test_fb01_bank(void)
{
    static uint8_t bank[FB01_BANK_SIZE];
    hexter_engine_t *e = hexter_engine_new(44100.0f);
    uint8_t cur[155];
    char name[11], *err = NULL;
    int n;

    build_fb01_bank(bank, 0);
    CHECK(fb01_bank_at(bank, FB01_BANK_SIZE, 0, 0, NULL), "the bank identifies as an FB-01 bank");

    n = hexter_engine_load_bank_memory(e, bank, FB01_BANK_SIZE, "fb01.syx", 0, &err);
    CHECK(n == 48, "an FB-01 bank loaded %d voices (%s)", n, err ? err : "no error");
    free(err); err = NULL;

    hexter_engine_get_program_name(e, 0, name);
    CHECK(!strcmp(name, "FB01 V1   "), "its seven-character name became '%s'", name);

    hexter_engine_select_program(e, 0);
    hexter_engine_get_current_patch(e, cur);

    /* algorithm 2 stands in as DX7 algorithm 14, stored as 13, and sends the
     * four operators to DX7 3, 4, 5 and 6 */
    CHECK(cur[134] == 13, "algorithm 2 became DX7 algorithm %d", cur[134] + 1);
    CHECK(cur[135] == 5, "feedback carried across as %d", cur[135]);

    /*
     * The FB-01 stores an operator's level as attenuation: 0 is loudest and
     * 127 is silence, the opposite of the DX7. These four were written as 0,
     * 32, 64 and 96, so they must come out loud to quiet, in that order, on
     * the DX7 operators the algorithm sends them to. Reading it the wrong way
     * round would turn every patch inside out and still load cleanly.
     */
    CHECK(cur[(6 - 3) * 21 + 16] == 75, "OP1, coded 24, became level %d on DX7 OP3",
          cur[(6 - 3) * 21 + 16]);
    CHECK(cur[(6 - 4) * 21 + 16] == 51, "OP2, coded 48, became %d on DX7 OP4",
          cur[(6 - 4) * 21 + 16]);
    CHECK(cur[(6 - 5) * 21 + 16] == 27, "OP3, coded 72, became %d on DX7 OP5",
          cur[(6 - 5) * 21 + 16]);
    CHECK(cur[(6 - 6) * 21 + 16] == 3, "OP4, coded 96, became %d on DX7 OP6",
          cur[(6 - 6) * 21 + 16]);
    /* the two operators with nowhere to come from stay silent */
    CHECK(cur[(6 - 1) * 21 + 16] == 0 && cur[(6 - 2) * 21 + 16] == 0,
          "the spare operators are silent");

    /*
     * Sustain is attenuation too, but it does not run to silence: the tuned
     * table stops at 35, so an operator with its sustain wound right down is
     * quiet rather than gone.
     */
    CHECK(cur[(6 - 3) * 21 + 5] == 99, "OP1's sustain, coded 0, became %d",
          cur[(6 - 3) * 21 + 5]);
    CHECK(cur[(6 - 4) * 21 + 5] == 35, "OP2's sustain, coded 15, became %d (the floor)",
          cur[(6 - 4) * 21 + 5]);
    CHECK(cur[(6 - 5) * 21 + 5] == 77, "OP3's sustain, coded 5, became %d",
          cur[(6 - 5) * 21 + 5]);

    /* detune runs 0-3 one way and 5-7 the other, around the DX7's 7 */
    CHECK(cur[(6 - 3) * 21 + 20] == 9, "detune 2 became %d", cur[(6 - 3) * 21 + 20]);
    CHECK(cur[(6 - 4) * 21 + 20] == 5, "detune 6 became %d", cur[(6 - 4) * 21 + 20]);

    /* the coarse detune becomes a frequency fine value */
    CHECK(cur[(6 - 3) * 21 + 18] == 1 && cur[(6 - 3) * 21 + 19] == 0,
          "ratio 1 with no coarse detune became coarse %d fine %d",
          cur[(6 - 3) * 21 + 18], cur[(6 - 3) * 21 + 19]);
    CHECK(cur[(6 - 5) * 21 + 19] == 41, "coarse detune 1 became fine %d",
          cur[(6 - 5) * 21 + 19]);

    /* envelope and scaling, through the tuned tables: the fastest attack the
     * FB-01 has is not quite the fastest the DX7 has */
    CHECK(cur[(6 - 3) * 21 + 0] == 98, "maximum attack became %d", cur[(6 - 3) * 21 + 0]);
    CHECK(cur[(6 - 3) * 21 + 13] == 6, "maximum rate scaling became %d", cur[(6 - 3) * 21 + 13]);
    CHECK(cur[(6 - 3) * 21 + 9] == 99 && cur[(6 - 3) * 21 + 11] == 3,
          "level scaling depth %d and curve %d", cur[(6 - 3) * 21 + 9], cur[(6 - 3) * 21 + 11]);
    CHECK(cur[(6 - 3) * 21 + 15] == 7, "velocity sensitivity became %d", cur[(6 - 3) * 21 + 15]);
    CHECK(cur[(6 - 3) * 21 + 14] == 2, "amplitude mod sensitivity became %d", cur[(6 - 3) * 21 + 14]);

    /*
     * Voice level. The LFO speed is a whole byte on the FB-01 and its fastest
     * is far slower than the DX7's, so the maximum comes across as about 21,
     * not 99. Reading it as seven bits and stretching it was the bug that made
     * every converted patch wobble three times too fast.
     */
    CHECK(cur[137] == 21, "the fastest FB-01 LFO became %d, not 99", cur[137]);
    CHECK(cur[142] == 0, "triangle LFO became wave %d", cur[142]);
    CHECK(cur[141] == 0, "the sync bit means what it says (%d)", cur[141]);
    CHECK(cur[143] == 6, "pitch mod sensitivity 6 carried across as %d", cur[143]);
    CHECK(cur[144] == 20, "four semitones down became transpose %d", cur[144]);

    /* the enable bits switch an operator off */
    hexter_engine_select_program(e, 1);
    hexter_engine_get_current_patch(e, cur);
    CHECK(cur[(6 - 6) * 21 + 16] == 0, "OP4 switched off is silent (level %d)",
          cur[(6 - 6) * 21 + 16]);
    CHECK(cur[(6 - 3) * 21 + 16] == 75, "while OP1 still sounds (level %d)",
          cur[(6 - 3) * 21 + 16]);

    /* And it makes a sound. The bank above is deliberately quiet, since its
     * levels are spread out to prove the attenuation is read the right way
     * round, so this uses one with every operator wide open. */
    {
        static uint8_t loud[FB01_BANK_SIZE];
        static float out[4410];
        hexter_engine_t *l = hexter_engine_new(44100.0f);
        hexter_event_t note;
        char *lerr = NULL;
        double peak = 0.0, quiet = 0.0;
        int i;

        build_fb01_bank(loud, 1);
        CHECK(hexter_engine_load_bank_memory(l, loud, FB01_BANK_SIZE, "fb01.syx", 0, &lerr) == 48,
              "the open bank loaded (%s)", lerr ? lerr : "no error");
        free(lerr);
        hexter_engine_select_program(l, 0);
        memset(&note, 0, sizeof(note));
        note.type = HEXTER_EV_NOTE_ON; note.a = 60; note.b = 100;
        render_seconds(l, out, 4410, &note, 1);
        for (i = 0; i < 4410; i++) if (fabs(out[i]) > peak) peak = fabs(out[i]);
        CHECK(peak > 0.01, "a converted FB-01 voice makes a sound (peak %.4f)", peak);
        hexter_engine_free(l);

        /* and the spread-out one is quieter, which is the attenuation showing
         * up in the sound rather than only in the bytes */
        hexter_engine_select_program(e, 0);
        render_seconds(e, out, 4410, &note, 1);
        for (i = 0; i < 4410; i++) if (fabs(out[i]) > quiet) quiet = fabs(out[i]);
        CHECK(quiet > 0.0 && quiet < peak / 4.0,
              "the attenuated bank is quieter (%.4f against %.4f)", quiet, peak);
    }

    /*
     * The same bank inside a standard MIDI file, which is how people often
     * save a dump. Every DX7-family format has always worked that way; this
     * one only worked as a bare .syx until now.
     */
    {
        static uint8_t mid[64 + FB01_BANK_SIZE + 8];
        hexter_engine_t *m = hexter_engine_new(44100.0f);
        char mname[11], *merr = NULL;
        size_t at, len = 0;
        int body = FB01_BANK_SIZE - 1;         /* everything after the F0 */

        memcpy(mid, "MThd", 4); len = 4;
        mid[len++] = 0; mid[len++] = 0; mid[len++] = 0; mid[len++] = 6;
        mid[len++] = 0; mid[len++] = 0;        /* format 0 */
        mid[len++] = 0; mid[len++] = 1;        /* one track */
        mid[len++] = 0; mid[len++] = 96;       /* division */
        memcpy(mid + len, "MTrk", 4); len += 4;
        mid[len++] = 0; mid[len++] = 0; mid[len++] = 0; mid[len++] = 0;  /* track length */
        mid[len++] = 0;                        /* delta time */

        at = len;
        mid[len++] = 0xf0;
        /* the event's own length, as MIDI files write it: seven bits a byte */
        mid[len++] = (uint8_t)(0x80 | ((body >> 7) & 0x7f));
        mid[len++] = (uint8_t)(body & 0x7f);
        memcpy(mid + len, bank + 1, (size_t)body); len += (size_t)body;

        CHECK(fb01_bank_at(mid, (long)len, (long)at, 2, NULL),
              "the bank is found inside a MIDI file");
        CHECK(!fb01_bank_at(mid, (long)len, (long)at, 0, NULL),
              "and not without allowing for the event's length bytes");
        CHECK(hexter_engine_load_bank_memory(m, mid, len, "fb01.mid", 0, &merr) == 48,
              "an FB-01 bank in a MIDI file loaded (%s)", merr ? merr : "no error");
        free(merr);
        hexter_engine_get_program_name(m, 0, mname);
        CHECK(!strcmp(mname, "FB01 V1   "), "with its voices intact, program 1 is '%s'", mname);
        hexter_engine_free(m);
    }

    /*
     * The manual gives two bank dumps. "Voice bank x" is the one above; "voice
     * bank 0" is the user bank, behind a shorter header, and it is the one an
     * owner is most likely to send, being the bank they can write to.
     */
    {
        static uint8_t bank0[FB01_BANK0_SIZE];
        hexter_engine_t *z = hexter_engine_new(44100.0f);
        fb01_bank_layout_t layout;
        char zname[11], *zerr = NULL;
        size_t zlen = fb01_as_bank0(bank, bank0);

        CHECK(zlen == FB01_BANK0_SIZE, "the user bank form is %zu bytes", zlen);
        CHECK(fb01_bank_at(bank0, (long)zlen, 0, 0, &layout),
              "it is recognized as an FB-01 bank");
        CHECK(layout.size == FB01_BANK0_SIZE && layout.voice_offset == FB01_BANK0_VOICE_OFF,
              "with its own layout (%ld bytes, voices at %ld)",
              layout.size, layout.voice_offset);
        CHECK(hexter_engine_load_bank_memory(z, bank0, zlen, "fb01-bank0.syx", 0, &zerr) == 48,
              "and loads its 48 voices (%s)", zerr ? zerr : "no error");
        free(zerr);
        hexter_engine_get_program_name(z, 0, zname);
        CHECK(!strcmp(zname, "FB01 V1   "), "program 1 is '%s'", zname);

        /* and it converts to exactly what the other form converts to, since it
         * is the same voices behind a different header */
        {
            uint8_t from_x[155], from_0[155];
            hexter_engine_select_program(e, 0);
            hexter_engine_get_current_patch(e, from_x);
            hexter_engine_select_program(z, 0);
            hexter_engine_get_current_patch(z, from_0);
            CHECK(!memcmp(from_x, from_0, 155),
                  "both forms give the same converted voice");
        }
        hexter_engine_free(z);
    }

    /* things that are not an FB-01 bank are not taken for one */
    {
        static uint8_t other[FB01_BANK_SIZE];

        memcpy(other, bank, FB01_BANK_SIZE);
        other[2] = 0x09;                       /* not the FB-01's id */
        CHECK(!fb01_bank_at(other, FB01_BANK_SIZE, 0, 0, NULL), "a different model is refused");
        memcpy(other, bank, FB01_BANK_SIZE);
        other[FB01_VOICE_OFFSET] = 0x02;       /* a voice that claims another length */
        CHECK(!fb01_bank_at(other, FB01_BANK_SIZE, 0, 0, NULL), "a wrong voice header is refused");
        CHECK(!fb01_bank_at(bank, FB01_BANK_SIZE - 1, 0, 0, NULL), "the wrong length is refused");
    }

    hexter_engine_free(e);
}

/* ---- operator waveforms ---- */

/*
 * Render one note on a brand new engine. It has to be a new engine: renders
 * on one engine are not bit-identical to each other, so comparing two of them
 * would show a difference whatever the waveforms did. Two fresh engines given
 * the same work do agree, which test_determinism_and_rom already checks.
 */
static void
render_with_waves(const uint8_t *w6, unsigned long program, float *out, uint32_t frames)
{
    hexter_engine_t *e = hexter_engine_new(44100.0f);
    hexter_event_t note;

    hexter_engine_load_bank_file(e, bank_path("dx7_roms.dx7"), 0, NULL);
    hexter_engine_select_program(e, program);
    if (w6) hexter_engine_set_op_waves(e, w6);

    memset(&note, 0, sizeof(note));
    note.type = HEXTER_EV_NOTE_ON; note.a = 60; note.b = 100;
    render_seconds(e, out, frames, &note, 1);
    hexter_engine_free(e);
}

static void
test_op_waveforms(void)
{
    hexter_engine_t *e = hexter_engine_new(44100.0f);
    static float sine[8820], shaped[8820], again[8820], each[DX7_WAVEFORMS][2048];
    uint8_t waves[6], got[6];
    char *err = NULL;
    double peak = 0.0;
    int i, w, x, distinct = 0;

    CHECK(hexter_engine_load_bank_file(e, bank_path("dx7_roms.dx7"), 0, &err) == 128,
          "load ROM for waveforms");
    free(err); err = NULL;

    /* a DX7 patch is all sines and says so */
    hexter_engine_select_program(e, 0);
    hexter_engine_get_op_waves(e, got);
    for (i = 0; i < 6; i++) if (got[i] != 0) break;
    CHECK(i == 6, "a DX7 patch starts out all sines");

    /* one non-sine operator changes the sound */
    memset(waves, 0, sizeof(waves));
    waves[0] = 1;                       /* OP1, a carrier in algorithm 1 */
    hexter_engine_set_op_waves(e, waves);
    hexter_engine_get_op_waves(e, got);
    CHECK(got[0] == 1 && got[1] == 0, "the waveform reads back (%d, %d)", got[0], got[1]);

    render_with_waves(NULL, 0, sine, 8820);
    render_with_waves(waves, 0, shaped, 8820);
    CHECK(memcmp(sine, shaped, sizeof(sine)) != 0,
          "a non-sine operator changes the rendered sound");
    for (i = 0; i < 8820; i++) if (fabs(shaped[i]) > peak) peak = fabs(shaped[i]);
    CHECK(peak > 0.01, "and it still makes a sound (peak %.4f)", peak);

    /* Asking for shape 0 reproduces the untouched render exactly, so the
     * difference above came from the waveform and not from having set one. */
    memset(waves, 0, sizeof(waves));
    render_with_waves(waves, 0, again, 8820);
    CHECK(!memcmp(sine, again, sizeof(sine)), "shape 0 renders exactly as before");

    /* all eight shapes differ from one another */
    for (w = 0; w < DX7_WAVEFORMS; w++) {
        for (i = 0; i < 6; i++) waves[i] = (uint8_t)w;
        render_with_waves(waves, 0, each[w], 2048);
    }
    for (w = 0; w < DX7_WAVEFORMS; w++)
        for (x = w + 1; x < DX7_WAVEFORMS; x++)
            if (memcmp(each[w], each[x], sizeof(each[0])) != 0) distinct++;
    CHECK(distinct == DX7_WAVEFORMS * (DX7_WAVEFORMS - 1) / 2,
          "the eight shapes all sound different (%d of %d pairs)",
          distinct, DX7_WAVEFORMS * (DX7_WAVEFORMS - 1) / 2);

    /* an out-of-range shape wraps rather than reading off the end of the table */
    memset(waves, 0, sizeof(waves));
    waves[2] = 200;
    hexter_engine_set_op_waves(e, waves);
    hexter_engine_get_op_waves(e, got);
    CHECK(got[2] == 200 % DX7_WAVEFORMS, "shape 200 became %d", got[2]);

    /* the waveforms belong to the program, so switching away and back keeps
     * the edit, and a different program is untouched by it */
    memset(waves, 0, sizeof(waves));
    waves[0] = 4;
    hexter_engine_select_program(e, 5);
    hexter_engine_set_op_waves(e, waves);
    hexter_engine_select_program(e, 6);
    hexter_engine_get_op_waves(e, got);
    CHECK(got[0] == 0, "program 6 is unaffected (%d)", got[0]);
    hexter_engine_select_program(e, 5);
    hexter_engine_get_op_waves(e, got);
    CHECK(got[0] == 4, "program 5 kept its waveform (%d)", got[0]);

    /* and a change is heard under a note that is already down */
    {
        hexter_engine_t *h1 = hexter_engine_new(44100.0f);
        hexter_engine_t *h2 = hexter_engine_new(44100.0f);
        static float held[11025], moved[11025];
        hexter_event_t note;
        uint8_t w6[6];

        hexter_engine_load_bank_file(h1, bank_path("dx7_roms.dx7"), 0, NULL);
        hexter_engine_load_bank_file(h2, bank_path("dx7_roms.dx7"), 0, NULL);
        memset(&note, 0, sizeof(note));
        note.type = HEXTER_EV_NOTE_ON; note.a = 60; note.b = 100;
        render_seconds(h1, held, 5512, &note, 1);
        render_seconds(h2, moved, 5512, &note, 1);
        CHECK(!memcmp(held, moved, sizeof(float) * 5512), "the same note starts the same in both");

        memset(w6, 0, sizeof(w6));
        w6[0] = 2;
        hexter_engine_set_op_waves(h2, w6);
        render_seconds(h1, held + 5512, 5513, NULL, 0);
        render_seconds(h2, moved + 5512, 5513, NULL, 0);
        CHECK(memcmp(held + 5512, moved + 5512, sizeof(float) * 5513) != 0,
              "a waveform change is heard under a held note");
        hexter_engine_free(h1);
        hexter_engine_free(h2);
    }

    hexter_engine_free(e);

    /* a TX81Z bank carries a waveform per operator, and the conversion has to
     * put each one on the DX7 operator that four-operator one became */
    {
        hexter_engine_t *t = hexter_engine_new(44100.0f);
        static uint8_t tx[6 + 4096 + 2];
        size_t tlen = build_4op_dump(tx, sizeof(tx), 0x04, 1);
        char *terr = NULL;

        CHECK(hexter_engine_load_bank_memory(t, tx, tlen, "tx81z.syx", 0, &terr) == 32,
              "loaded a TX81Z bank with waveforms (%s)", terr ? terr : "no error");
        free(terr);
        hexter_engine_select_program(t, 0);
        hexter_engine_get_op_waves(t, got);
        /* algorithm 2 sends four-operator OP1-OP4 to DX7 4, 5, 3, 6, and the
         * shapes written into the dump were OP1 3, OP2 5, OP3 1, OP4 7 */
        CHECK(got[3] == 3, "OP1's shape reached DX7 OP4 (%d)", got[3]);
        CHECK(got[4] == 5, "OP2's shape reached DX7 OP5 (%d)", got[4]);
        CHECK(got[2] == 1, "OP3's shape reached DX7 OP3 (%d)", got[2]);
        CHECK(got[5] == 7, "OP4's shape reached DX7 OP6 (%d)", got[5]);
        CHECK(got[0] == 0 && got[1] == 0, "the two spare operators stay sines");
        hexter_engine_free(t);
    }

    /* A four-operator dump sent over MIDI, which is how you would send one
     * from the hardware, converts the same way a file does. */
    {
        hexter_engine_t *t = hexter_engine_new(44100.0f);
        static uint8_t tx[6 + 4096 + 2];
        hexter_event_t ev;
        static float out[256];
        char nm[11];

        build_4op_dump(tx, sizeof(tx), 0x04, 1);
        CHECK(hexter_event_from_midi(tx, 6 + 4096 + 2, 0, &ev),
              "a TX81Z dump parses as sysex");
        hexter_engine_render(t, out, 256, &ev, 1);
        hexter_engine_get_program_name(t, 0, nm);
        CHECK(!strcmp(nm, "FOUR OP 1 "), "it loaded over MIDI, program 1 is '%s'", nm);
        hexter_engine_get_op_waves(t, got);
        CHECK(got[3] == 3 && got[4] == 5 && got[2] == 1 && got[5] == 7,
              "and brought its waveforms (%d %d %d %d)", got[3], got[4], got[2], got[5]);

        /* a DX7 dump over the top puts every operator back to a sine, without
         * needing a program change to notice */
        {
            dx7_patch_t rom[128];
            static uint8_t bulk[4104];
            char *berr = NULL;

            CHECK(dx7_patchbank_load(bank_path("dx7_roms.dx7"), rom, 128, &berr) == 128,
                  "load ROM to send over it");
            free(berr);
            bulk[0] = 0xF0; bulk[1] = 0x43; bulk[2] = 0x00;
            bulk[3] = 0x09; bulk[4] = 0x20; bulk[5] = 0x00;
            memcpy(bulk + 6, rom, 4096);
            bulk[4102] = (uint8_t)dx7_bulk_dump_checksum(bulk + 6, 4096);
            bulk[4103] = 0xF7;
            hexter_event_from_midi(bulk, sizeof(bulk), 0, &ev);
            hexter_engine_render(t, out, 256, &ev, 1);
            hexter_engine_get_program_name(t, 0, nm);
            CHECK(!strcmp(nm, "BRASS   1 "), "the DX7 dump landed, program 1 is '%s'", nm);
            hexter_engine_get_op_waves(t, got);
            for (i = 0; i < 6; i++) if (got[i] != 0) break;
            CHECK(i == 6, "and the sounding patch went back to sines");
        }
        hexter_engine_free(t);
    }

    /* the same, for a bank file loaded over a TX81Z one with no program change */
    {
        hexter_engine_t *t = hexter_engine_new(44100.0f);
        static uint8_t tx[6 + 4096 + 2];
        size_t tlen = build_4op_dump(tx, sizeof(tx), 0x04, 1);
        char *terr = NULL;

        hexter_engine_load_bank_memory(t, tx, tlen, "tx81z.syx", 0, &terr);
        free(terr); terr = NULL;
        hexter_engine_select_program(t, 0);
        hexter_engine_get_op_waves(t, got);
        CHECK(got[3] == 3, "the TX81Z bank is in place (%d)", got[3]);

        CHECK(hexter_engine_load_bank_file(t, bank_path("dx7_roms.dx7"), 0, &terr) == 128,
              "a DX7 bank loads over it");
        free(terr);
        hexter_engine_get_op_waves(t, got);
        for (i = 0; i < 6; i++) if (got[i] != 0) break;
        CHECK(i == 6, "and the sounding patch goes back to sines at once");
        hexter_engine_free(t);
    }

    /* a DX100 bank has nothing in those bytes, so every operator is a sine */
    {
        hexter_engine_t *t = hexter_engine_new(44100.0f);
        static uint8_t dx[6 + 4096 + 2];
        size_t dlen = build_4op_dump(dx, sizeof(dx), 0x03, 0);
        char *derr = NULL;

        CHECK(hexter_engine_load_bank_memory(t, dx, dlen, "dx100.syx", 0, &derr) == 32,
              "loaded a DX100 bank (%s)", derr ? derr : "no error");
        free(derr);
        hexter_engine_select_program(t, 0);
        hexter_engine_get_op_waves(t, got);
        for (i = 0; i < 6; i++) if (got[i] != 0) break;
        CHECK(i == 6, "a DX100 bank is all sines");
        hexter_engine_free(t);
    }
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
    {
        uint8_t w[6] = { 1, 0, 6, 0, 3, 0 };
        hexter_engine_set_op_waves(a, w);
    }

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

    {
        uint8_t wa[6], wb[6];
        hexter_engine_get_op_waves(a, wa); hexter_engine_get_op_waves(b, wb);
        CHECK(!memcmp(wa, wb, 6) && wb[2] == 6,
              "operator waveforms restored (OP3 shape %d)", wb[2]);
    }

    memset(&ev, 0, sizeof(ev));
    ev.type = HEXTER_EV_NOTE_ON; ev.a = 60; ev.b = 100;
    hexter_engine_render(a, oa, 1024, &ev, 1);
    hexter_engine_render(b, ob, 1024, &ev, 1);
    CHECK(!memcmp(oa, ob, sizeof(oa)), "restored engine renders identically");

    /* a state written before the operator waveforms existed stops at the old
     * length and says so in its payload field; it still loads, all sines */
    {
        hexter_engine_t *c = hexter_engine_new(48000.0f);
        static uint8_t old_state[HEXTER_STATE_SIZE_V1];
        uint8_t wc[6];
        int i;

        memcpy(old_state, state, HEXTER_STATE_SIZE_V1);
        old_state[8]  = (HEXTER_STATE_SIZE_V1 - 12) & 0xff;
        old_state[9]  = ((HEXTER_STATE_SIZE_V1 - 12) >> 8) & 0xff;
        old_state[10] = old_state[11] = 0;
        CHECK(hexter_engine_state_load(c, old_state, sizeof(old_state)),
              "a version 1 state still loads");
        CHECK(hexter_engine_get_program(c) == 17, "and restores its program (%d)",
              hexter_engine_get_program(c));
        hexter_engine_get_op_waves(c, wc);
        for (i = 0; i < 6; i++) if (wc[i] != 0) break;
        CHECK(i == 6, "with every operator a sine");
        hexter_engine_free(c);
    }

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
    test_algorithm();
    test_4op_bank();
    test_op_waveforms();
    test_fb01_bank();
    test_state();
    test_voices();
    test_sample_rates();

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
