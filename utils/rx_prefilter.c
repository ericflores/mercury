/* rx_prefilter -- the receive thread's audio pre-processing, on a file.
 *
 * mercury runs the carrier notch and then the impulse blanker on the captured
 * audio before any decoder (modem/modem.c, rx_thread).  This applies the same
 * two modules to a raw file (s16le, mono, 8 kHz), so a tool that decodes from
 * a file can be given what a decoder would get: utils/mfsk_burst_file for the
 * MFSK floor, which utils/rx_bench does not cover.
 *
 *   rx_prefilter filter <in.raw> <out.raw> [nt] [bK]
 *       nt   the automatic carrier notch (modem/rx_notch.c)
 *       bK   the impulse blanker at threshold K (modem/rx_blanker.c)
 *   rx_prefilter pad <seconds> <in.raw> <out.raw>
 *       silence before the file: the notch needs a second of a carrier
 *       before it engages, as on the air it has had
 *   rx_prefilter level <in.raw>
 *       prints the rms of the file in dBFS (0 dBFS = 32767)
 *
 * Copyright (C) 2026 Rhizomatica
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rx_blanker.h"
#include "rx_notch.h"

#define FS 8000

static int16_t *load(const char *path, long *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    *n = ftell(f) / 2;
    fseek(f, 0, SEEK_SET);
    int16_t *x = malloc(sizeof(int16_t) * (size_t)(*n > 0 ? *n : 1));
    if (!x || fread(x, 2, (size_t)*n, f) != (size_t)*n) { fprintf(stderr, "%s: read error\n", path); fclose(f); return NULL; }
    fclose(f);
    return x;
}

static int save(const char *path, const int16_t *x, long n)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return 1; }
    fwrite(x, 2, (size_t)n, f);
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    long n;
    if (argc == 3 && !strcmp(argv[1], "level")) {
        int16_t *x = load(argv[2], &n);
        if (!x) return 1;
        double p = 0.0;
        for (long i = 0; i < n; i++) p += (double)x[i] * x[i];
        printf("%.2f\n", n ? 10.0 * log10(p / n / (32767.0 * 32767.0) + 1e-30) : -300.0);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "pad")) {
        int16_t *x = load(argv[3], &n);
        if (!x) return 1;
        long lead = (long)(atof(argv[2]) * FS);
        if (lead < 0) lead = 0;
        int16_t *y = calloc((size_t)(lead + n), sizeof(int16_t));
        if (!y) return 1;
        memcpy(y + lead, x, sizeof(int16_t) * (size_t)n);
        return save(argv[4], y, lead + n);
    }
    if (argc >= 4 && !strcmp(argv[1], "filter")) {
        int16_t *x = load(argv[2], &n);
        if (!x) return 1;
        int notch = 0;
        float k = 0.0f;
        for (int a = 4; a < argc; a++) {
            if (!strcmp(argv[a], "nt")) notch = 1;
            else if (argv[a][0] == 'b' && atof(argv[a] + 1) >= 1.0) k = (float)atof(argv[a] + 1);
            else { fprintf(stderr, "unknown filter '%s': nt, bK\n", argv[a]); return 1; }
        }
        if (notch) {
            static rx_notch_t nt;
            rx_notch_init(&nt, true, FS);
            for (long pos = 0; pos < n; pos += 160) rx_notch_process(&nt, x + pos, (int)(n - pos < 160 ? n - pos : 160));
            fprintf(stderr, "notch: engaged %llu, released %llu\n", (unsigned long long)nt.engaged,
                    (unsigned long long)nt.released);
        }
        if (k > 0.0f) {
            rx_blanker_t bl;
            rx_blanker_init(&bl, k, FS);
            for (long pos = 0; pos < n; pos += 160) rx_blanker_process(&bl, x + pos, (int)(n - pos < 160 ? n - pos : 160));
            fprintf(stderr, "blanker: %.4f %% blanked, %llu impulses\n", 100.0 * rx_blanker_fraction(&bl),
                    (unsigned long long)bl.impulses);
        }
        return save(argv[3], x, n);
    }
    fprintf(stderr, "usage: %s filter <in.raw> <out.raw> [nt] [bK] | pad <seconds> <in.raw> <out.raw> | level <in.raw>\n",
            argv[0]);
    return 1;
}
