/* Convert every voice of every FB-01 bank on the command line with hexter's
 * converter, and write them out as packed 128-byte DX7 voices, in bank order.
 * Sean Bolton's FB_TX_Conv reads the same banks and writes the same shape, so
 * the two streams line up voice for voice. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "dx7_voice_fb01.h"
#include "dx7_voice.h"
#include "dx7_voice_data.h"

int main(int argc, char **argv)
{
    long total = 0;
    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb");
        if (!f) { fprintf(stderr, "cannot open %s\n", argv[a]); return 1; }
        fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t *d = malloc(len);
        if (fread(d, 1, len, f) != (size_t)len) { fprintf(stderr, "short read\n"); return 1; }
        fclose(f);

        fb01_bank_layout_t lay;
        if (!fb01_bank_at(d, len, 0, 0, &lay)) {
            fprintf(stderr, "%s: not an FB-01 bank\n", argv[a]); return 1;
        }
        for (int v = 0; v < FB01_BANK_VOICES; v++) {
            const uint8_t *p = d + lay.voice_offset + v * FB01_VOICE_STRIDE
                                 + FB01_VOICE_PARAM_OFF;
            uint8_t unpacked[155];
            dx7_patch_t packed;
            fb01_voice_to_dx7(p, unpacked);
            dx7_patch_pack(unpacked, &packed, 0);
            fwrite(&packed, 1, 128, stdout);
            total++;
        }
        free(d);
    }
    fprintf(stderr, "hexter: %ld voices\n", total);
    return 0;
}
