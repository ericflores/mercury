/* test_ofdm_cprecycle -- recycling the cyclic prefix as a last decode attempt
 * (freedv_set_cp_recycle, docs/improvements/19-cp-recycling.md).
 *
 * What must hold:
 *  - it is off unless asked for, and a guard that makes no sense is off;
 *  - on a noiseless burst the recycled rows equal the stock rows: the pairs
 *    that are averaged really are copies of each other;
 *  - on, it never loses a frame the receiver without it delivers (it is the
 *    last attempt; the stock one runs first and unchanged), and where the
 *    stock decode works nothing reaches it;
 *  - at a mode's cliff, fed the same samples as a receiver with proposal 09
 *    alone, it adds frames, each delivered through decode path 3;
 *  - on a two-path channel with the long guard no frame is lost.
 *
 * Both receivers of a trial are fed the same samples; noise is a seeded
 * generator, so the counts are the same on every run.
 *
 * Copyright (C) 2026 Rhizomatica
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <complex.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "freedv_api.h"
#include "freedv_api_internal.h"
#include "codec2_ofdm.h"
#include "ofdm_internal.h"

void setUp(void) {}
void tearDown(void) {}

static uint64_t rng_s;
static double uni(void)
{
    rng_s ^= rng_s >> 12; rng_s ^= rng_s << 25; rng_s ^= rng_s >> 27;
    return (double)((rng_s * 2685821657736338717ULL) >> 11) / 9007199254740992.0;
}
static double gauss(void)
{
    double u = uni(), v = uni();
    if (u < 1e-300) u = 1e-300;
    return sqrt(-2.0 * log(u)) * cos(2.0 * M_PI * v);
}

#define PEAK 4000.0

/* DATAC3 on white noise: its cliff with the 09 attempt alone is about
 * -5 dB; at -5.5 dB (32 trials, seed 2026) that receiver delivers 1 and the
 * one with recycling 5.  With a second path 16 samples (2 ms) behind at 0.7
 * the 09-only receiver delivers 20 of 32 at -4 dB. */
#define CLIFF_DB (-5.5)
#define CLIFF_TRIALS 32
#define CLIFF_MIN_ADDED 3
#define ECHO_DB (-4.0)

/* One burst of `mode` at snr3k dB in AWGN (snr3k > 90: no noise), with noise
 * before and after, in buf; the payload sent in payload.  When echo_amp > 0 a
 * copy of the burst delayed by echo_delay samples at that amplitude is added
 * before the noise; the SNR is that of the direct path.  Returns the sample
 * count. */
static int make_trial(struct freedv *tx, double snr3k, int echo_delay,
                      double echo_amp, short *buf, unsigned char *payload)
{
    int nbytes = freedv_get_bits_per_modem_frame(tx) / 8;
    int ntx = freedv_get_n_tx_modem_samples(tx);
    short *b = malloc(sizeof(short) * (size_t)(4 * ntx));
    for (int i = 0; i < nbytes - 2; i++) payload[i] = (unsigned char)(uni() * 256.0);
    uint16_t crc = freedv_gen_crc16(payload, nbytes - 2);
    payload[nbytes - 2] = (unsigned char)(crc >> 8);
    payload[nbytes - 1] = (unsigned char)(crc & 0xff);
    int n = freedv_rawdatapreambletx(tx, b);
    freedv_rawdatatx(tx, b + n, payload);
    n += ntx;
    n += freedv_rawdatapostambletx(tx, b + n);

    double ps = 0.0, pk = 0.0;
    for (int i = 0; i < n; i++) {
        ps += (double)b[i] * b[i];
        if (fabs((double)b[i]) > pk) pk = fabs((double)b[i]);
    }
    double g = PEAK / pk;
    ps = ps / n * g * g;
    double sigma = snr3k > 90.0 ? 0.0
                 : sqrt(ps / (pow(10.0, snr3k / 10.0) * (3000.0 / 4000.0)));
    int lead = 3200, total = lead + n + n;
    for (int i = 0; i < total; i++) {
        double s = sigma > 0.0 ? sigma * gauss() : 0.0;
        if (i >= lead && i < lead + n) s += b[i - lead] * g;
        if (echo_amp > 0.0 && i - echo_delay >= lead && i - echo_delay < lead + n)
            s += echo_amp * b[i - echo_delay - lead] * g;
        if (s > 32767.0) s = 32767.0;
        if (s < -32768.0) s = -32768.0;
        buf[i] = (short)lrint(s);
    }
    free(b);
    return total;
}

/* Feed buf to rx; 1 if a CRC-valid frame equal to payload came out.  *path
 * gets the attempt that delivered it. */
static int run_rx(struct freedv *rx, const short *buf, int total,
                  const unsigned char *payload, int nbytes, int *path)
{
    unsigned char out[2048];
    short chunk[8192];
    int pos = 0, got = 0;
    freedv_set_sync(rx, FREEDV_SYNC_UNSYNC);
    while (pos < total) {
        int nin = freedv_nin(rx);
        if (nin > 8192) break;
        for (int i = 0; i < nin; i++) chunk[i] = pos + i < total ? buf[pos + i] : 0;
        pos += nin;
        int nb = (int)freedv_rawdatarx(rx, out, chunk);
        if (nb > 0 && !(freedv_get_rx_status(rx) & FREEDV_RX_BIT_ERRORS) &&
            !memcmp(out, payload, (size_t)nbytes - 2)) {
            got = 1;
            if (path) *path = freedv_get_rx_decode_path(rx);
        }
    }
    return got;
}

typedef struct { int base, cp, cp_only, lost, by_cp_path; } tally_t;

/* The same samples to a receiver with chanest alone (the baseline: proposal
 * 09) and one with chanest and recycling at `guard`. */
static tally_t sweep(int mode, int chanest, int guard, double snr3k,
                     int echo_delay, double echo_amp, int trials, uint64_t seed)
{
    struct freedv *tx = freedv_open(mode), *rx0 = freedv_open(mode), *rx1 = freedv_open(mode);
    TEST_ASSERT_NOT_NULL(tx); TEST_ASSERT_NOT_NULL(rx0); TEST_ASSERT_NOT_NULL(rx1);
    freedv_set_frames_per_burst(tx, 1);
    freedv_set_frames_per_burst(rx0, 1);
    freedv_set_frames_per_burst(rx1, 1);
    freedv_set_chanest_alt(rx0, chanest);
    freedv_set_chanest_alt(rx1, chanest);
    freedv_set_cp_recycle(rx1, guard);
    TEST_ASSERT_EQUAL_INT(guard, freedv_get_cp_recycle(rx1));
    TEST_ASSERT_EQUAL_INT(0, freedv_get_cp_recycle(rx0));
    int nbytes = freedv_get_bits_per_modem_frame(tx) / 8;
    int ntx = freedv_get_n_tx_modem_samples(tx);
    short *buf = malloc(sizeof(short) * (size_t)(3200 + 8 * ntx));
    unsigned char payload[2048];
    tally_t t = {0};
    rng_s = seed;
    for (int k = 0; k < trials; k++) {
        int total = make_trial(tx, snr3k, echo_delay, echo_amp, buf, payload);
        int path = -1;
        int a = run_rx(rx0, buf, total, payload, nbytes, NULL);
        int b = run_rx(rx1, buf, total, payload, nbytes, &path);
        t.base += a; t.cp += b;
        if (b && !a) t.cp_only++;
        if (a && !b) t.lost++;
        if (b && path == 3) t.by_cp_path++;
    }
    free(buf);
    freedv_close(tx); freedv_close(rx0); freedv_close(rx1);
    return t;
}

static void test_off_by_default(void)
{
    struct freedv *f = freedv_open(FREEDV_MODE_DATAC3);
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_INT(0, freedv_get_cp_recycle(f));
    freedv_set_cp_recycle(f, -3);                       /* negative: off */
    TEST_ASSERT_EQUAL_INT(0, freedv_get_cp_recycle(f));
    freedv_set_cp_recycle(f, 49);                       /* DATAC3's prefix is 48 */
    TEST_ASSERT_EQUAL_INT(0, freedv_get_cp_recycle(f));
    freedv_set_cp_recycle(f, 100000);
    TEST_ASSERT_EQUAL_INT(0, freedv_get_cp_recycle(f));
    freedv_set_cp_recycle(f, 20);
    TEST_ASSERT_EQUAL_INT(20, freedv_get_cp_recycle(f));
    freedv_set_cp_recycle(f, 0);
    TEST_ASSERT_EQUAL_INT(0, freedv_get_cp_recycle(f));
    freedv_close(f);

    f = freedv_open(FREEDV_MODE_QAM16C2);               /* a 32-sample prefix */
    TEST_ASSERT_NOT_NULL(f);
    freedv_set_cp_recycle(f, 33);
    TEST_ASSERT_EQUAL_INT(0, freedv_get_cp_recycle(f));
    freedv_set_cp_recycle(f, 20);
    TEST_ASSERT_EQUAL_INT(20, freedv_get_cp_recycle(f));
    freedv_close(f);
}

/* With no noise and no echo the pairs the averaging combines are equal, so
 * the recycled rows must equal the stock rows to within float error.  This is
 * what shows the twins are the right samples (a window off by one would not
 * match).  Compared on every modem frame of the burst. */
static double norm2(complex float z) { return crealf(z) * crealf(z) + cimagf(z) * cimagf(z); }

static void test_recycled_rows_match_clean(void)
{
    struct freedv *tx = freedv_open(FREEDV_MODE_DATAC3), *rx = freedv_open(FREEDV_MODE_DATAC3);
    struct freedv *ref = freedv_open(FREEDV_MODE_DATAC3);  /* recycling off */
    TEST_ASSERT_NOT_NULL(tx); TEST_ASSERT_NOT_NULL(rx); TEST_ASSERT_NOT_NULL(ref);
    freedv_set_frames_per_burst(tx, 1);
    freedv_set_frames_per_burst(rx, 1);
    freedv_set_frames_per_burst(ref, 1);
    freedv_set_chanest_alt(rx, OFDM_CHANEST_T4F3);
    freedv_set_chanest_alt(ref, OFDM_CHANEST_T4F3);
    freedv_set_cp_recycle(rx, 1);
    int ntx = freedv_get_n_tx_modem_samples(tx);
    short *buf = malloc(sizeof(short) * (size_t)(3200 + 8 * ntx));
    unsigned char payload[2048], out[2048];
    short chunk[8192];
    rng_s = 5;
    int total = make_trial(tx, 100.0, 0, 0.0, buf, payload);
    struct OFDM *ofdm = rx->ofdm;
    int pos = 0, compared = 0;
    double worst = 0.0;
    freedv_set_sync(rx, FREEDV_SYNC_UNSYNC);
    while (pos < total) {
        int nin = freedv_nin(rx);
        for (int i = 0; i < nin; i++) chunk[i] = pos + i < total ? buf[pos + i] : 0;
        pos += nin;
        /* Put the window 30 samples into the 48-sample prefix, so that both
         * sides of the averaging (18 samples on the tail side, 29 on the head
         * side with a guard of 1) are exercised; normally the window sits a
         * few samples in. */
        if (ofdm->sync_state == synced) ofdm->sample_point = ofdm->timing_est + 30;
        if (ref->ofdm->sync_state == synced) ref->ofdm->sample_point = ref->ofdm->timing_est + 30;
        freedv_rawdatarx(rx, out, chunk);
        freedv_rawdatarx(ref, out, chunk);
        /* What the stock and the 09 paths produce is bit for bit the same
         * with recycling on. */
        size_t nsym = (size_t)ofdm->rowsperframe * (size_t)ofdm->nc;
        TEST_ASSERT_EQUAL_INT(0, memcmp(ofdm->rx_np, ref->ofdm->rx_np, nsym * sizeof(complex float)));
        TEST_ASSERT_EQUAL_INT(0, memcmp(ofdm->rx_amp, ref->ofdm->rx_amp, nsym * sizeof(float)));
        TEST_ASSERT_EQUAL_INT(0, memcmp(ofdm->rx_np_alt, ref->ofdm->rx_np_alt, nsym * sizeof(complex float)));
        TEST_ASSERT_EQUAL_INT(0, memcmp(ofdm->rx_amp_alt, ref->ofdm->rx_amp_alt, nsym * sizeof(float)));
        if (ofdm->sync_state != synced) continue;
        for (int r = 0; r < ofdm->ns + 3; r++) {
            double e = 0.0, p = 0.0;
            for (int c = 1; c <= ofdm->nc; c++) {
                e += norm2(ofdm->rx_sym[r][c] - ofdm->rx_sym_cp[r][c]);
                p += norm2(ofdm->rx_sym[r][c]);
            }
            TEST_ASSERT_TRUE(p > 0.0);
            if (sqrt(e / p) > worst) worst = sqrt(e / p);
        }
        compared++;
    }
    char msg[120];
    snprintf(msg, sizeof msg, "worst relative row difference %.2e over %d modem frames", worst, compared);
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(compared >= 4, msg);
    TEST_ASSERT_TRUE_MESSAGE(worst < 0.1, msg);
    free(buf);
    freedv_close(tx); freedv_close(rx); freedv_close(ref);
}

/* Well above the cliff every frame decodes on the stock attempt: neither
 * the 09 attempt nor the recycled one is reached, and nothing changes. */
static void test_strong_signal_uses_stock_path(void)
{
    tally_t t = sweep(FREEDV_MODE_DATAC3, OFDM_CHANEST_T4F3, 4, 5.0, 0, 0.0, 6, 11);
    TEST_ASSERT_EQUAL_INT(6, t.base);
    TEST_ASSERT_EQUAL_INT(6, t.cp);
    TEST_ASSERT_EQUAL_INT(0, t.by_cp_path);
}

/* At the cliff: no frame the 09-only receiver delivers is lost, and frames
 * are added, each through the recycled attempt and each the frame that was
 * sent (run_rx compares the bytes). */
static void check_cliff(int mode, int chanest, int guard, double snr3k, int trials, int min_added)
{
    tally_t t = sweep(mode, chanest, guard, snr3k, 0, 0.0, trials, 2026);
    char msg[160];
    snprintf(msg, sizeof msg, "mode %d chanest %d guard %d at %.1f dB: 09 alone %d, with recycling %d, added %d, lost %d",
             mode, chanest, guard, snr3k, t.base, t.cp, t.cp_only, t.lost);
    TEST_MESSAGE(msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, t.lost, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(t.cp - t.base, t.cp_only, msg);
    TEST_ASSERT_TRUE_MESSAGE(t.cp_only >= min_added, msg);
    TEST_ASSERT_TRUE_MESSAGE(t.by_cp_path >= t.cp_only, msg);
}

static void test_datac3_cliff_guard4(void)  { check_cliff(FREEDV_MODE_DATAC3, OFDM_CHANEST_T4F3, 4, CLIFF_DB, CLIFF_TRIALS, CLIFF_MIN_ADDED); }

/* (Guard 20 on white noise delivers the same frames as guard 4 here, the
 * window sitting only a few samples into the prefix, so the long guard is
 * exercised on the two-path channel below.) */

/* A second path 16 samples (2 ms) behind at 0.7 amplitude: with the long
 * guard the head-side samples that hold the echo are left alone, so no frame
 * that the 09-only receiver delivers is lost (and, being a later attempt,
 * none can be). */
static void test_two_path_guard20_loses_nothing(void)
{
    tally_t t = sweep(FREEDV_MODE_DATAC3, OFDM_CHANEST_T4F3, 20, ECHO_DB, 16, 0.7, 32, 2026);
    char msg[160];
    snprintf(msg, sizeof msg, "two-path, guard 20 at %.1f dB: 09 alone %d, with recycling %d, added %d, lost %d",
             ECHO_DB, t.base, t.cp, t.cp_only, t.lost);
    TEST_MESSAGE(msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, t.lost, msg);
    TEST_ASSERT_TRUE_MESSAGE(t.base >= 1, msg);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_off_by_default);
    RUN_TEST(test_recycled_rows_match_clean);
    RUN_TEST(test_strong_signal_uses_stock_path);
    RUN_TEST(test_datac3_cliff_guard4);
    RUN_TEST(test_two_path_guard20_loses_nothing);
    return UNITY_END();
}
