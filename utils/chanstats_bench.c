/* chanstats_bench -- what the receiver's channel statistics read on known
 * channels (docs/improvements/10-channel-telemetry.md).
 *
 *   chanstats_bench MODE trials chan lo hi step [tone_hz tone_db]
 *
 *   chan  awgn (lo..hi are SNR3k in dB) or a watterson preset name
 *         (lo..hi are the noise density No; the achieved SNR3k is printed).
 *   tone  an unmodulated carrier at tone_hz, tone_db relative to the burst.
 *
 * Environment: CHANSTATS_TXCLIP=0 sends without the Hilbert clipper,
 * CHANSTATS_CLIPGAIN=g drives it harder than stock (DATAC1: 2.7, amplitude 145E3: set for DATAC1),
 * CHANSTATS_TXBPF=0/1 the transmit filter.
 *
 * One burst per trial, decoded by one receiver with the noise statistics on
 * (freedv_set_noise_stats); the statistics of the packet are read when it
 * finishes, decoded or not, as the chanstats: line does.  Per row: how many
 * packets finished and decoded, then the median and the 10 % / 90 % points of
 * snr, dopp (Hz), dly (ms), evm and evm_excess (dB), and the share of the
 * packets in each class of chan_classify() (reported, no policy).
 *
 * The bursts are normalised to one peak (HEADROOM_PEAK) as in rx_bench, and
 * the channel is the project's Watterson model through chanutil.
 *
 * Copyright (C) 2026 Rhizomatica
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_common.h"
#include "chanutil.h"
#include "chan_classify.h"
#include "freedv_api.h"

#define FS 8000.0
#define HEADROOM_PEAK 4000.0

static bench_rng rng = {24681357911ULL};
static double gauss(void) { return bench_rng_gauss(&rng); }

static int cmpf(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

/* median [p10..p90] of n values, sorted in place */
static void quant(float *v, int n, float *m, float *lo, float *hi)
{
    if (n < 1) { *m = *lo = *hi = 0.0f; return; }
    qsort(v, (size_t)n, sizeof(float), cmpf);
    *m = v[n / 2];
    *lo = v[n / 10];
    *hi = v[n - 1 - n / 10];
}

/* The rung's decode threshold, as datalink_arq/arq_protocol.h states it. */
static float mode_thresh(int mode)
{
    switch (mode) {
    case FREEDV_MODE_DATAC4: return -6.0f;
    case FREEDV_MODE_DATAC3: return -1.0f;
    case FREEDV_MODE_DATAC1: return 3.0f;
    case FREEDV_MODE_DATAC17: return 7.0f;
    case FREEDV_MODE_QAM16C2: return 13.0f;
    default: return NAN;
    }
}

int main(int argc, char **argv)
{
    if (argc < 7) {
        fprintf(stderr, "usage: chanstats_bench MODE trials chan lo hi step [tone_hz tone_db]\n");
        return 1;
    }
    int mode = bench_mode_from_name(argv[1]);
    int trials = atoi(argv[2]);
    int chan = chanutil_preset_from_name(argv[3]);
    double lo = atof(argv[4]), hi = atof(argv[5]), step = atof(argv[6]);
    double tone_hz = argc > 8 ? atof(argv[7]) : 0.0;
    double tone_db = argc > 8 ? atof(argv[8]) : -99.0;
    if (mode < 0 || chan < 0) { fprintf(stderr, "bad mode/channel\n"); return 1; }
    float thresh = mode_thresh(mode);

    struct freedv *tx = freedv_open(mode), *rx = freedv_open(mode);
    freedv_set_frames_per_burst(tx, 1);
    freedv_set_frames_per_burst(rx, 1);
    /* CHANSTATS_TXCLIP=0: the transmitter without its Hilbert clipper */
    if (getenv("CHANSTATS_TXCLIP") && !atoi(getenv("CHANSTATS_TXCLIP")))
        freedv_set_tx_clip_config(tx, false, 2.5f, 0.8f, 135E3f);
    /* CHANSTATS_CLIPGAIN=g: the clipper driven harder than stock (2.5) */
    if (getenv("CHANSTATS_CLIPGAIN"))
        freedv_set_tx_clip_config(tx, true, (float)atof(getenv("CHANSTATS_CLIPGAIN")), 0.8f, 145E3f);
    if (getenv("CHANSTATS_TXBPF")) freedv_set_tx_bpf(tx, atoi(getenv("CHANSTATS_TXBPF")));
    freedv_set_noise_stats(rx, 1);
    int nbytes = freedv_get_bits_per_modem_frame(tx) / 8;
    int nburst = freedv_get_n_tx_preamble_modem_samples(tx) + freedv_get_n_tx_modem_samples(tx) +
                 freedv_get_n_tx_postamble_modem_samples(tx);
    int lead = (int)(0.4 * FS), ntot = lead + nburst + nburst;
    short *burst = malloc(sizeof(short) * (size_t)(nburst + 4)), *buf = malloc(sizeof(short) * (size_t)ntot);
    unsigned char *payload = malloc((size_t)nbytes), *out = malloc((size_t)nbytes + 8);
    float *v[7];
    for (int k = 0; k < 7; k++) v[k] = malloc(sizeof(float) * (size_t)trials);

    printf("%s, chan %s, tone %.0f Hz %+.0f dB, rung threshold %+.0f dB, %d trials a row\n", argv[1],
           chanutil_preset_name(chan), tone_hz, tone_db, (double)thresh, trials);
    printf("%-7s %-7s %5s %4s  %-7s %-17s %-17s %-17s %-17s %-17s  share of packets by class (%%)\n", chan == CHAN_AWGN ? "SNR3k" : "No",
           "meas", "pkts", "ok", "snr", "dopp Hz", "dly ms", "evm dB", "excess dB", "resid dB");

    for (double x = lo; x <= hi + 1e-6; x += step) {
        int npk = 0, nok = 0, nstat = 0, cls[CHAN_N_CLASSES] = {0};
        double meas_sum = 0.0;
        chanutil_t *ch = chan != CHAN_AWGN ? chanutil_open(chan, (float)x, 1234u) : NULL;
        for (int t = 0; t < trials; t++) {
            bench_fill_payload(payload, nbytes, &rng);
            int n = bench_make_burst(tx, burst, payload);
            double ps = 0.0, pk = 0.0;
            for (int i = 0; i < n; i++) {
                ps += (double)burst[i] * burst[i];
                if (fabs((double)burst[i]) > pk) pk = fabs((double)burst[i]);
            }
            ps /= n;
            double gn = HEADROOM_PEAK / pk;
            for (int i = 0; i < n; i++) burst[i] = (short)lrint(burst[i] * gn);
            ps *= gn * gn;
            double snr = x;
            if (ch) {
                float m = 0.0f;
                chanutil_run(ch, (int16_t *)burst, n, &m);
                snr = m;
            }
            meas_sum += snr;
            double sigma = sqrt(ps / (pow(10.0, snr / 10.0) * (3000.0 / (FS / 2.0))));
            double ta = tone_db > -90.0 ? sqrt(2.0 * ps * pow(10.0, tone_db / 10.0)) : 0.0;
            double tph = 2.0 * M_PI * bench_rng_uniform(&rng);
            for (int i = 0; i < ntot; i++) {
                double s = 0.0;
                int bi = i - lead;
                if (bi >= 0 && bi < n) s = burst[bi];
                if (!ch || bi < 0 || bi >= n) s += sigma * gauss();
                else (void)gauss();
                if (ta > 0.0) s += ta * cos(2.0 * M_PI * tone_hz * i / FS + tph);
                if (s > 32767.0) s = 32767.0;
                if (s < -32768.0) s = -32768.0;
                buf[i] = (short)lrint(s);
            }
            freedv_set_sync(rx, FREEDV_SYNC_UNSYNC);
            int pos = 0, got = 0;
            struct freedv_rx_frame_stats fs;
            while (pos < ntot && !got) {
                int nin = freedv_nin(rx);
                short chunk[8192];
                if (nin > 8192) break;
                for (int i = 0; i < nin; i++) chunk[i] = pos + i < ntot ? buf[pos + i] : 0;
                pos += nin;
                (void)freedv_rawdatarx(rx, out, chunk);
                int st = freedv_get_rx_status(rx);
                if ((st & (FREEDV_RX_BITS | FREEDV_RX_BIT_ERRORS)) && freedv_get_rx_frame_stats(rx, &fs)) got = 1;
            }
            if (!got) continue;
            npk++;
            nok += fs.ok;
            if ((fs.chan_valid & 7) == 7) {
                v[0][nstat] = fs.snr_db;
                v[1][nstat] = fs.dopp_hz;
                v[2][nstat] = fs.dly_ms;
                v[3][nstat] = fs.evm_db;
                v[4][nstat] = fs.evm_excess_db;
                v[5][nstat] = fs.evm_resid_db;
                nstat++;
            }
            /* what the modem has: the gap and blanker statistics are not built */
            chan_class_in_t in = {.snr_db = fs.snr_db, .thresh_db = thresh, .nv_peak_db = fs.noise_peak_db,
                                  .have_spread = (fs.chan_valid & 7) == 7, .evm_resid_db = fs.evm_resid_db,
                                  .noise_evm_db = fs.evm_noise_db,
                                  .dly_ms = fs.dly_ms, .dopp_hz = fs.dopp_hz, .cp_ms = fs.cp_ms,
                                  .blank_frac = -1.0f, .gap_kurt = -1.0f, .gap_peaks = -1};
            cls[chan_classify(&in)]++;
        }
        if (ch) chanutil_close(ch);
        float m[6], l[6], h[6];
        for (int k = 0; k < 6; k++) quant(v[k], nstat, &m[k], &l[k], &h[k]);
        printf("%+6.1f  %+6.2f  %5d %4d  %6.1f ", x, meas_sum / trials, npk, nok, (double)m[0]);
        for (int k = 1; k < 6; k++) printf(" %6.2f [%5.2f %5.2f]", (double)m[k], (double)l[k], (double)h[k]);
        printf("  ");
        for (int c = 0; c < CHAN_N_CLASSES; c++)
            if (cls[c]) printf(" %s %.0f", chan_class_name((chan_class_t)c), npk ? 100.0 * cls[c] / npk : 0.0);
        printf("   (stats on %d)\n", nstat);
        fflush(stdout);
    }
    freedv_close(rx);
    freedv_close(tx);
    return 0;
}
