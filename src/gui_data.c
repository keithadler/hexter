/* hexter DSSI software synthesizer GUI
 *
 * Copyright (C) 2004, 2009, 2011, 2012 Sean Bolton and others.
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "hexter_types.h"
#include "hexter.h"
#include "dx7_voice.h"
#include "gui_main.h"
#include "dx7_voice_data.h"

#include "dx7_bank.h"

/*
 * encode_7in6
 *
 * encode a block of 7-bit data, in base64-ish style
 */
char *
encode_7in6(uint8_t *data, int length)
{
    char *buffer;
    int in, reg, above, below, shift, out;
    int outchars = (length * 7 + 5) / 6;
    unsigned int sum = 0;

    if (!(buffer = (char *)malloc(25 + outchars)))
        return NULL;

    out = snprintf(buffer, 12, "%d ", length);

    in = reg = above = below = 0;
    while (outchars) {
        if (above == 6) {
            buffer[out] = base64[reg >> 7];
            reg &= 0x7f;
            above = 0;
            out++;
            outchars--;
        }
        if (below == 0) {
            if (in < length) {
                reg |= data[in] & 0x7f;
                sum += data[in];
            }
            below = 7;
            in++;
        }
        shift = 6 - above;
        if (below < shift) shift = below;
        reg <<= shift;
        above += shift;
        below -= shift;
    }

    snprintf(buffer + out, 12, " %d", sum);

    return buffer;
}

void
gui_data_patches_init(void)
{
    if (!patches)
        patches = (dx7_patch_t *)malloc(128 * sizeof(dx7_patch_t));
    hexter_data_patches_init(patches);
    patch_section_dirty[0] = 0;
    patch_section_dirty[1] = 0;
    patch_section_dirty[2] = 0;
    patch_section_dirty[3] = 0;
}

void
gui_data_patches_free(void)
{
    if (patches) free(patches);
}

void
gui_data_mark_dirty_patch_sections(int start_patch, int end_patch)
{
    int i, block;
    for (i = start_patch; i <= end_patch; ) {
        block = i >> 5;
        patch_section_dirty[block] = 1;
        i = (block + 1) << 5;
    }
}

void
gui_data_send_dirty_patch_sections(void)
{
    int block;
    char *p;
    char key[9];
    for (block = 0; block < 4; block++) {
        if (patch_section_dirty[block]) {
            if ((p = encode_7in6((uint8_t *)&patches[block << 5],
                                 32 * DX7_VOICE_SIZE_PACKED))) {
                snprintf(key, 9, "patches%d", block);
                lo_send(osc_host_address, osc_configure_path, "ss", key, p);
                free(p);
                patch_section_dirty[block] = 0;
            }
        }
    }
}

int
gui_data_save(char *filename, int type, int start, int end, char **message)
{
    FILE *fh;
    char buffer[20];
    int i, j;
    uint8_t *patch;
    int checksum = 0;

    GUIDB_MESSAGE(DB_IO, " gui_data_save: attempting to save '%s'\n", filename);

    if ((fh = fopen(filename, "wb")) == NULL) {
        if (message) *message = dssp_error_message("could not open file '%s'for writing", filename);
        return 0;
    }

    if (type == 0) { /* sys-ex */
        buffer[0] = 0xf0;
        buffer[1] = 0x43;
        buffer[2] = 0x00;
        buffer[3] = 0x09;
        buffer[4] = 0x10;
        buffer[5] = 0x00;
        if (fwrite(buffer, 1, 6, fh) != 6) {
            fclose(fh);
            if (message) *message = dssp_error_message("error while writing sys-ex header: %s", strerror(errno));
            return 0;
        }
    }

    for (i = start; i <= end; i++) {
        patch = (uint8_t *)&patches[i];

        for (j = 0; j < DX7_VOICE_SIZE_PACKED; checksum -= patch[j++]);

        if (fwrite(patch, 1, DX7_VOICE_SIZE_PACKED, fh) != DX7_VOICE_SIZE_PACKED) {
            fclose(fh);
            if (message) *message = dssp_error_message("error while writing file: %s", strerror(errno));
            return 0;
        }
    }

    if (type == 0) { /* sys-ex */
        buffer[0] = checksum & 0x7f;
        buffer[1] = 0xf7;
        if (fwrite(buffer, 1, 2, fh) != 2) {
            fclose(fh);
            if (message) *message = dssp_error_message("error while writing sys-ex footer: %s", strerror(errno));
            return 0;
        }
    }

    fclose(fh);

    if (message) {
        *message = dssp_error_message("wrote %d patches", end - start + 1);
    }
    return 1;
}

/*
 * gui_data_load
 */
int
gui_data_load(const char *filename, int position, char **message)
{
    int tmpcount = 0;

    GUIDB_MESSAGE(DB_IO, " gui_data_load: attempting to load '%s'\n", filename);

    tmpcount = dx7_patchbank_load(filename, &patches[position], 128 - position,
                                  message);

    if (!tmpcount) {
        /* no patches loaded (message was set by dx7_patchbank_load()) */
        return 0;
    }
    gui_data_mark_dirty_patch_sections(position, position + tmpcount - 1);

    if (message) {
        *message = dssp_error_message("loaded %d patches", tmpcount);
    }

    return tmpcount;
}

void
gui_data_send_edit_buffer(void)
{
    char *p;

    if ((p = encode_7in6((uint8_t *)&edit_buffer, sizeof(edit_buffer)))) {
        lo_send(osc_host_address, osc_configure_path, "ss", "edit_buffer", p);
        free(p);
    }
}

void
gui_data_send_edit_buffer_off(void)
{
    lo_send(osc_host_address, osc_configure_path, "ss", "edit_buffer", "off");
}

void
gui_data_send_performance_buffer(uint8_t *performance_buffer)
{
    char *p;

    if ((p = encode_7in6(performance_buffer, DX7_PERFORMANCE_SIZE))) {
        lo_send(osc_host_address, osc_configure_path, "ss", "performance", p);
        free(p);
    }
}
