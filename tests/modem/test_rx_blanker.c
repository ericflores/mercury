/* test_rx_blanker -- the impulse blanker (modem/rx_blanker.c,
 * docs/improvements/08-impulse-blanker.md).
 *
 * What must hold:
 *  - k = 0 leaves the buffer untouched, with no delay;
 *  - on, the output is the input delayed by the lookahead wherever nothing
 *    was blanked;
 *  - a clean burst of an OFDM mode, clipped or not, arriving after a second
 *    of low noise, at full scale or 30 dB down, loses (almost) no sample:
 *    the burst is adopted as the new level, not blanked at its start;
 *  - 2 ms noise bursts 25 dB above the signal lose almost all their energy,
 *    and almost nothing else is touched;
 *  - impulses that fill a tenth of the time trip the per-second cap and the
 *    rest of each second passes;
 *  - impulses do not capture the level estimate.
 *
 * Copyright (C) 2026 Rhizomatica
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "freedv_api.h"
#include "rx_blanker.h"

void setUp(void) {}
void tearDown(void) {}

#define FS 8000
#define L RX_BLANKER_LOOKAHEAD

static uint64_t rng_s = 88172645463325252ULL;
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
static int16_t clip16(double v)
{
    if (v > 32767.0) v = 32767.0;
    if (v < -32768.0) v = -32768.0;
    return (int16_t)lrint(v);
}

/* One burst of `mode` scaled to `peak`, in out; returns its length. */
static int make_burst(int mode, double peak, double *out, int cap)
{
    struct freedv *tx = freedv_open(mode);
    TEST_ASSERT_NOT_NULL(tx);
    freedv_set_frames_per_burst(tx, 1);
    int nbytes = freedv_get_bits_per_modem_frame(tx) / 8;
    int ntx = freedv_get_n_tx_modem_samples(tx);
    short *b = malloc(sizeof(short) * (size_t)(4 * ntx));
    unsigned char payload[2048];
    for (int i = 0; i < nbytes - 2; i++) payload[i] = (unsigned char)(uni() * 256.0);
    uint16_t crc = freedv_gen_crc16(payload, nbytes - 2);
    payload[nbytes - 2] = (unsigned char)(crc >> 8);
    payload[nbytes - 1] = (unsigned char)(crc & 0xff);
    int n = freedv_rawdatapreambletx(tx, b);
    freedv_rawdatatx(tx, b + n, payload);
    n += ntx;
    n += freedv_rawdatapostambletx(tx, b + n);
    double pk = 0.0;
    for (int i = 0; i < n; i++) if (fabs((double)b[i]) > pk) pk = fabs((double)b[i]);
    TEST_ASSERT_TRUE(n <= cap);
    for (int i = 0; i < n; i++) out[i] = b[i] * peak / pk;
    free(b);
    freedv_close(tx);
    return n;
}

static void test_off_is_passthrough(void)
{
    rx_blanker_t b;
    int16_t x[4000], ref[4000];
    for (int i = 0; i < 4000; i++) x[i] = ref[i] = clip16(3000.0 * gauss());
    x[1000] = ref[1000] = 32000;
    rx_blanker_init(&b, 0.0f, FS);
    TEST_ASSERT_EQUAL_INT(0, rx_blanker_process(&b, x, 4000));
    TEST_ASSERT_EQUAL_MEMORY(ref, x, sizeof(x));
    rx_blanker_init(&b, -3.0f, FS);
    TEST_ASSERT_EQUAL_INT(0, rx_blanker_process(&b, x, 4000));
    TEST_ASSERT_EQUAL_MEMORY(ref, x, sizeof(x));
}

/* Steady noise, threshold 6: nothing is blanked and the output is the input
 * L samples later, whatever the chunking. */
static void test_on_is_a_pure_delay(void)
{
    enum { N = 3 * FS };
    static int16_t x[N], ref[N];
    rx_blanker_t b;
    for (int i = 0; i < N; i++) x[i] = ref[i] = clip16(1000.0 * gauss());
    rx_blanker_init(&b, 6.0f, FS);
    int z = 0, pos = 0, chunk = 1;
    while (pos < N) {
        int n = chunk < N - pos ? chunk : N - pos;
        z += rx_blanker_process(&b, &x[pos], n);
        pos += n;
        chunk = chunk * 3 + 1;
        if (chunk > 700) chunk = 7;
    }
    TEST_ASSERT_EQUAL_INT(0, z);
    for (int i = 0; i < L; i++) TEST_ASSERT_EQUAL_INT(0, x[i]);
    TEST_ASSERT_EQUAL_MEMORY(ref, &x[L], sizeof(int16_t) * (N - L));
}

/* A clean burst after 1 s of noise 40 dB below it.  The burst's own peaks
 * (unclipped QAM16C2: 11.4 dB) and its sudden start must not be blanked. */
static void check_clean_burst(int mode, const char *name, double peak)
{
    enum { CAP = 12 * FS };
    static double sig[CAP];
    static int16_t x[CAP + 2 * FS];
    int n = make_burst(mode, peak, sig, CAP);
    double rms = 0.0;
    for (int i = 0; i < n; i++) rms += sig[i] * sig[i];
    rms = sqrt(rms / n);
    double sigma = rms * 0.01;
    int lead = FS, total = lead + n + FS / 2;
    for (int i = 0; i < total; i++) {
        double v = sigma * gauss();
        if (i >= lead && i < lead + n) v += sig[i - lead];
        x[i] = clip16(v);
    }
    rx_blanker_t b;
    rx_blanker_init(&b, 6.0f, FS);
    int z = rx_blanker_process(&b, x, total);
    char msg[160];
    snprintf(msg, sizeof(msg), "%s at peak %.0f: %d of %d burst samples blanked", name, peak, z, n);
    /* fewer than 0.01 % */
    TEST_ASSERT_TRUE_MESSAGE((double)z < 1e-4 * n + 1.0, msg);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, b.suspensions, msg);
}

static void test_clean_signal_untouched(void)
{
    check_clean_burst(FREEDV_MODE_DATAC1, "DATAC1", 30000.0);
    check_clean_burst(FREEDV_MODE_DATAC1, "DATAC1", 1000.0);
    check_clean_burst(FREEDV_MODE_QAM16C2, "QAM16C2", 30000.0);
    check_clean_burst(FREEDV_MODE_QAM16C2, "QAM16C2", 1000.0);
    check_clean_burst(FREEDV_MODE_DATAC15, "DATAC15", 30000.0);
    check_clean_burst(FREEDV_MODE_DATAC16, "DATAC16", 1000.0);
}

/* Steady noise as the "signal", with 2 ms bursts 25 dB above it, three a
 * second.  Almost all of the impulses' energy is removed and almost nothing
 * else is touched.  (Counting samples says less: an impulse of noise has
 * samples below the threshold at its edges, which pass and do no harm.)  The
 * sequence has two impulses 6 ms apart: judging "does it last" on the mean
 * of the lookahead took them for a burst and let the next three through. */
static void test_impulses_blanked(void)
{
    enum { N = 10 * FS };
    static int16_t x[N], clean[N];
    static uint8_t is_imp[N];
    double rms = 300.0, ia = rms * pow(10.0, 25.0 / 20.0);
    int left = 0, nimp = 0;
    double e_imp = 0.0;
    for (int i = 0; i < N; i++) {
        double v = rms * gauss();
        clean[i] = clip16(v);
        if (!left && i > FS && uni() < 3.0 / FS) left = FS / 500;
        if (left) { v += ia * gauss(); left--; is_imp[i] = 1; nimp++; } else is_imp[i] = 0;
        x[i] = clip16(v);
        if (is_imp[i]) e_imp += (double)x[i] * x[i];
    }
    TEST_ASSERT_TRUE(nimp > 100);
    rx_blanker_t b;
    rx_blanker_init(&b, 6.0f, FS);
    rx_blanker_process(&b, x, N);
    int imp_zero = 0, other_zero = 0, others = 0;
    double e_left = 0.0;
    for (int i = 0; i + L < N; i++) {
        int zero = x[i + L] == 0;
        if (is_imp[i]) { imp_zero += zero; e_left += (double)x[i + L] * x[i + L]; }
        else { others++; other_zero += zero && clean[i] != 0; }
    }
    printf("    impulses: %d of %d impulse samples zeroed, %.3f %% of their energy left; %d of %d others zeroed\n",
           imp_zero, nimp, 100.0 * e_left / e_imp, other_zero, others);
    TEST_ASSERT_TRUE(imp_zero >= (int)(0.95 * nimp));
    TEST_ASSERT_TRUE(e_left < 0.01 * e_imp);
    /* the hold after each impulse: 1 ms after 30 impulses in 72000 samples */
    TEST_ASSERT_TRUE(other_zero < (int)(0.005 * others));
    TEST_ASSERT_EQUAL_UINT64(0, b.suspensions);
}

/* Impulses that fill 10 % of the time: blanking them all would remove a
 * tenth of the audio.  The cap stops it at 5 % of each second and the rest of
 * that second passes. */
static void test_cap_suspends(void)
{
    enum { N = 6 * FS };
    static int16_t x[N];
    double rms = 300.0, ia = rms * pow(10.0, 25.0 / 20.0);
    for (int i = 0; i < N; i++) {
        double v = rms * gauss();
        if (i > FS && i % (FS / 50) < FS / 500) v += ia * gauss();
        x[i] = clip16(v);
    }
    rx_blanker_t b;
    rx_blanker_init(&b, 6.0f, FS);
    int z = rx_blanker_process(&b, x, N);
    printf("    cap: %d of %d samples blanked, %llu suspensions\n", z, N, (unsigned long long)b.suspensions);
    TEST_ASSERT_TRUE(b.suspensions >= 3);
    TEST_ASSERT_TRUE_MESSAGE(z <= (int)(0.05 * N) + 6 * 2, "more than the cap was blanked");
}

/* After 50 impulses the level is within 1 dB of what it is without them. */
static void test_level_not_captured(void)
{
    enum { N = 20 * FS };
    static int16_t x[N], y[N];
    double rms = 300.0, ia = rms * pow(10.0, 25.0 / 20.0);
    int count = 0;
    for (int i = 0; i < N; i++) {
        double v = rms * gauss();
        x[i] = clip16(v);
        if (i > FS && i % (3 * FS / 8) < FS / 500 && count < 50 * (FS / 500)) { v += ia * gauss(); count++; }
        y[i] = clip16(v);
    }
    rx_blanker_t a, b;
    rx_blanker_init(&a, 6.0f, FS);
    rx_blanker_init(&b, 6.0f, FS);
    rx_blanker_process(&a, x, N);
    rx_blanker_process(&b, y, N);
    double db = 20.0 * log10((double)b.level / (double)a.level);
    printf("    level with impulses %+.2f dB against without (%llu impulses blanked)\n", db,
           (unsigned long long)b.impulses);
    TEST_ASSERT_TRUE(b.impulses >= 45);
    TEST_ASSERT_TRUE(fabs(db) < 1.0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_off_is_passthrough);
    RUN_TEST(test_on_is_a_pure_delay);
    RUN_TEST(test_clean_signal_untouched);
    RUN_TEST(test_impulses_blanked);
    RUN_TEST(test_cap_suspends);
    RUN_TEST(test_level_not_captured);
    return UNITY_END();
}
