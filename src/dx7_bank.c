/* hexter DX7 software synthesizer
 *
 * Copyright (C) 2004, 2009, 2011 Sean Bolton and others.
 * DX7 patchbank loading code by Martin Tarenskeen.
 * Copyright (C) 2026 Keith Adler (engine extraction, in-memory parsing).
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
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "hexter_types.h"
#include "dx7_voice.h"
#include "dx7_voice_data.h"
#include "dx7_bank.h"

int
dx7_bulk_dump_checksum(const uint8_t *data, int length)
{
    int sum = 0;
    int i;

    for (i = 0; i < length; sum -= data[i++]);
    return sum & 0x7F;
}

/* case-insensitive "does filename end with ext" (ext includes the dot) */
static int
has_extension(const char *filename, const char *ext)
{
    size_t fl, el, i;

    if (!filename) return 0;
    fl = strlen(filename);
    el = strlen(ext);
    if (fl < el) return 0;
    for (i = 0; i < el; i++) {
        char a = filename[fl - el + i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return 0;
    }
    return 1;
}

int
dx7_patchbank_parse(uint8_t *raw_patch_data, long filelength,
                    const char *filename, dx7_patch_t *firstpatch,
                    int maxpatches, char **errmsg)
{
    int count;
    int patchstart;
    int midshift;
    int datastart;
    int i;
    int op;

    if (filelength == 0) {
        if (errmsg) *errmsg = strdup("patch file has zero length");
        return 0;
    } else if (filelength > 2097152) {
        if (errmsg) *errmsg = strdup("patch file is too large");
        return 0;
    } else if (filelength < 128) {
        if (errmsg) *errmsg = strdup("patch file is too small");
        return 0;
    }

    /* check if the file is a standard MIDI file */
    if (raw_patch_data[0] == 0x4d &&    /* "M" */
        raw_patch_data[1] == 0x54 &&    /* "T" */
        raw_patch_data[2] == 0x68 &&    /* "h" */
        raw_patch_data[3] == 0x64)      /* "d" */
        midshift = 2;
    else
        midshift = 0;

    /* scan SysEx or MIDI file for SysEx header(s) */
    count = 0;
    datastart = 0;
    for (patchstart = 0; patchstart + midshift + 5 < filelength; patchstart++) {

        if (raw_patch_data[patchstart] == 0xf0 &&
            raw_patch_data[patchstart + 1 + midshift] == 0x43 &&
            raw_patch_data[patchstart + 2 + midshift] <= 0x0f &&
            raw_patch_data[patchstart + 3 + midshift] == 0x09 &&
            raw_patch_data[patchstart + 5 + midshift] == 0x00 &&
            patchstart + 4103 + midshift < filelength &&
            raw_patch_data[patchstart + 4103 + midshift] == 0xf7) {  /* DX7 32 voice dump */

            memmove(raw_patch_data + count * DX7_VOICE_SIZE_PACKED,
                    raw_patch_data + patchstart + 6 + midshift, 4096);
            count += 32;
            patchstart += (DX7_DUMP_SIZE_VOICE_BULK - 1);

        } else if (raw_patch_data[patchstart] == 0xf0 &&
                   raw_patch_data[patchstart + midshift + 1] == 0x43 &&
                   raw_patch_data[patchstart + midshift + 2] <= 0x0f &&
                   raw_patch_data[patchstart + midshift + 4] == 0x01 &&
                   raw_patch_data[patchstart + midshift + 5] == 0x1b &&
                   patchstart + midshift + 162 < filelength &&
                   raw_patch_data[patchstart + midshift + 162] == 0xf7) {  /* DX7 single voice (edit buffer) dump */

            unsigned char buf[DX7_VOICE_SIZE_PACKED]; /* to avoid overlap in dx7_patch_pack() */

            dx7_patch_pack(raw_patch_data + patchstart + midshift + 6,
                           (dx7_patch_t *)buf, 0);
            memcpy(raw_patch_data + count * DX7_VOICE_SIZE_PACKED,
                   buf, DX7_VOICE_SIZE_PACKED);

            count += 1;
            patchstart += (DX7_DUMP_SIZE_VOICE_SINGLE - 1);
        }
    }

    /* assume raw DX7/TX7 data if no SysEx header was found. */
    /* assume the user knows what he is doing ;-) */

    if (count == 0)
        count = filelength / DX7_VOICE_SIZE_PACKED;

    /* Dr.T and Steinberg TX7 file needs special treatment */
    if ((has_extension(filename, ".tx7") || has_extension(filename, ".snd"))
        && filelength == 8192) {

        count = 32;
        filelength = 4096;
    }

    /* Transform XSyn file also needs special treatment */
    if (has_extension(filename, ".bnk") && filelength == 8192) {

        for (i = 0; i < 32; i++) {
            memmove(raw_patch_data + 128 * i, raw_patch_data + 256 * i, 128);
        }
        count = 32;
        filelength = 4096;
    }

    /* Steinberg Synthworks DX7 SND */
    if (has_extension(filename, ".snd") && filelength == 5216) {

        count = 32;
        filelength = 4096;
    }

    /* Voyetra SIDEMAN DX/TX
     * Voyetra Patchmaster DX7/TX7 */
    if ((filelength == 9816 || filelength == 5663) &&
        raw_patch_data[0] == 0xdf &&
        raw_patch_data[1] == 0x05 &&
        raw_patch_data[2] == 0x01 && raw_patch_data[3] == 0x00) {

        count = 32;
        datastart = 0x60f;
    }

    /* Yamaha DX200 editor .DX2 */
    if (has_extension(filename, ".dx2") && filelength == 326454) {

        memmove(raw_patch_data + 16384, raw_patch_data + 34, 128 * 381);
        for (count = 0; count < 128; count++) {
            for (op = 0; op < 6; op++) {
                for (i = 0; i < 8; i++) {
                    raw_patch_data[17 * (5 - op) + i + 128 * count] =
                        raw_patch_data[16384 + 35 * op + 76 + i + 381 * count];
                }
                raw_patch_data[17 * (5 - op) + 8 + 128 * count] =
                    raw_patch_data[16384 + 35 * op + 84 + 381 * count] - 21;
                raw_patch_data[17 * (5 - op) + 9 + 128 * count] =
                    raw_patch_data[16384 + 35 * op + 87 + 381 * count];
                raw_patch_data[17 * (5 - op) + 10 + 128 * count] =
                    raw_patch_data[16384 + 35 * op + 88 + 381 * count];
                raw_patch_data[17 * (5 - op) + 11 + 128 * count] =
                    raw_patch_data[16384 + 35 * op + 85 + 381 * count] +
                    raw_patch_data[16384 + 35 * op + 86 + 381 * count] * 4;
                raw_patch_data[17 * (5 - op) + 12 + 128 * count] =
                    raw_patch_data[16384 + 35 * op + 89 + 381 * count] +
                    raw_patch_data[16384 + 35 * op + 75 + 381 * count] * 8;
                if (raw_patch_data[16384 + 35 * op + 71 + 381 * count] > 3)
                    raw_patch_data[16384 + 35 * op + 71 + 381 * count] = 3;
                raw_patch_data[17 * (5 - op) + 13 + 128 * count] =
                    raw_patch_data[16384 + 35 * op + 71 + 381 * count] / 2 +
                    raw_patch_data[16384 + 35 * op + 91 + 381 * count] * 4;
                raw_patch_data[17 * (5 - op) + 14 + 128 * count] =
                    raw_patch_data[16384 + 35 * op + 90 + 381 * count];
                raw_patch_data[17 * (5 - op) + 15 + 128 * count] =
                    raw_patch_data[16384 + 35 * op + 72 + 381 * count] +
                    raw_patch_data[16384 + 35 * op + 73 + 381 * count] * 2;
                raw_patch_data[17 * (5 - op) + 16 + 128 * count] =
                    raw_patch_data[16384 + 35 * op + 74 + 381 * count];
            }
            for (i = 0; i < 4; i++) {
                raw_patch_data[102 + i + 128 * count] =
                    raw_patch_data[16384 + 26 + i + 381 * count];
            }
            for (i = 0; i < 4; i++) {
                raw_patch_data[106 + i + 128 * count] =
                    raw_patch_data[16384 + 32 + i + 381 * count];
            }
            raw_patch_data[110 + 128 * count] =
                raw_patch_data[16384 + 17 + 381 * count];
            raw_patch_data[111 + 128 * count] =
                raw_patch_data[16384 + 18 + 381 * count] +
                raw_patch_data[16384 + 38 + 381 * count] * 8;
            for (i = 0; i < 4; i++) {
                raw_patch_data[112 + i + 128 * count] =
                    raw_patch_data[16384 + 20 + i + 381 * count];
            }
            raw_patch_data[116 + 128 * count] =
                raw_patch_data[16384 + 24 + 381 * count] +
                raw_patch_data[16384 + 19 + 381 * count] * 2 +
                raw_patch_data[16384 + 25 + 381 * count] * 16;
            raw_patch_data[117 + 128 * count] =
                raw_patch_data[16384 + 37 + 381 * count] - 36;
            for (i = 0; i < 10; i++) {
                raw_patch_data[118 + i + 128 * count] =
                    raw_patch_data[16384 + i + 381 * count];
            }
        }

        count = 128;
        filelength = 16384;
        datastart = 0;
    }

    /* finally, copy patchdata to the right location */
    if (count > maxpatches)
        count = maxpatches;
    if (count <= 0) {
        if (errmsg) *errmsg = strdup("no patches found in file");
        return 0;
    }

    memcpy(firstpatch, raw_patch_data + datastart, 128 * count);
    return count;
}

int
dx7_patchbank_load(const char *filename, dx7_patch_t *firstpatch,
                   int maxpatches, char **errmsg)
{
    FILE *fp;
    long filelength;
    unsigned char *raw_patch_data = NULL;
    int count;

    if ((fp = fopen(filename, "rb")) == NULL) {
        if (errmsg) *errmsg = dssp_error_message("could not open file '%s' for reading: %s", filename, strerror(errno));
        return 0;
    }

    if (fseek(fp, 0, SEEK_END) ||
        (filelength = ftell(fp)) == -1 ||
        fseek(fp, 0, SEEK_SET)) {
        if (errmsg) *errmsg = dssp_error_message("couldn't get length of patch file: %s", strerror(errno));
        fclose(fp);
        return 0;
    }
    if (filelength == 0) {
        if (errmsg) *errmsg = strdup("patch file has zero length");
        fclose(fp);
        return 0;
    } else if (filelength > 2097152) {
        if (errmsg) *errmsg = strdup("patch file is too large");
        fclose(fp);
        return 0;
    }

    /* pad the buffer so the DX2 transform has room to spread out */
    if (!(raw_patch_data = (unsigned char *)calloc(1, filelength + 16384 + 128 * 381))) {
        if (errmsg) *errmsg = strdup("couldn't allocate memory for raw patch file");
        fclose(fp);
        return 0;
    }

    if (fread(raw_patch_data, 1, filelength, fp) != (size_t)filelength) {
        if (errmsg) *errmsg = dssp_error_message("short read on patch file: %s", strerror(errno));
        free(raw_patch_data);
        fclose(fp);
        return 0;
    }
    fclose(fp);

    count = dx7_patchbank_parse(raw_patch_data, filelength, filename,
                                firstpatch, maxpatches, errmsg);
    free(raw_patch_data);
    return count;
}
