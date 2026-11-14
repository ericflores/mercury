/* test_noise_weight -- noise-aware soft bits
 * (freedv_set_noise_weight, docs/improvements/07-noise-aware-llr.md).
 *
 * What must hold:
 *  - it is off unless asked for;
 *  - the per-carrier noise profile built from the pilots is flat on white
 *    noise and peaks on the carrier a CW interferer sits on;
 *  - on, it never loses a frame the receiver without it delivers: the stock
 *    decode runs first and unchanged, and a frame it delivers comes out
 *    through decode path 0 on both receivers;
 *  - with no interferer the two receivers deliver the same frames at the
 *    mode's cliff (a flat profile runs no extra attempt);
 *  - with a carrier 3 dB below the signal inside the band, where the stock
 *    receiver delivers nothing, the weighted attempt delivers, through decode
 *    path 5.
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
#define FS 8000.0

/* DATAC1: 27 carriers 62.5 Hz apart centred on 1500 Hz, so carrier c
 * (0-based) is at 1500 + (c - 13) * 62.5 Hz.  A tone at 1650 Hz lies between
 * carriers 15 (1625 Hz) and 16 (1687.5 Hz). */
#define TONE_HZ 1650.0
#define TONE_C_LO 15
#define TONE_C_HI 16

/* One burst of `mode` at snr3k dB in AWGN with noise before and after, and,
 * when tone_db > -90, an unmodulated carrier at tone_hz, tone_db relative to
 * the burst's power, over the whole buffer.  Returns the sample count. */
static int make_trial(struct freedv *tx, double snr3k, double tone_hz, double tone_db,
                      short *buf, unsigned char *payload)
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
    double sigma = sqrt(ps / (pow(10.0, snr3k / 10.0) * (3000.0 / 4000.0)));
    double ta = tone_db > -90.0 ? sqrt(2.0 * ps * pow(10.0, tone_db / 10.0)) : 0.0;
    double tph = 2.0 * M_PI * uni();
    int lead = 3200, total = lead + n + n;
    for (int i = 0; i < total; i++) {
        double s = sigma * gauss();
        if (i >= lead && i < lead + n) s += b[i - lead] * g;
        if (ta > 0.0) s += ta * cos(2.0 * M_PI * tone_hz * i / FS + tph);
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

static struct freedv *open_rx(int mode, int nw)
{
    struct freedv *rx = freedv_open(mode);
    TEST_ASSERT_NOT_NULL(rx);
    freedv_set_frames_per_burst(rx, 1);
    freedv_set_noise_weight(rx, nw);
    return rx;
}

static float median_of(const float *v, int n)
{
    float s[64];
    memcpy(s, v, sizeof(float) * (size_t)n);
    for (int a = 1; a < n; a++)
        for (int b = a; b > 0 && s[b] < s[b - 1]; b--) { float t = s[b]; s[b] = s[b - 1]; s[b - 1] = t; }
    return s[(n - 1) / 2];
}

static void test_off_by_default(void)
{
    struct freedv *f = freedv_open(FREEDV_MODE_DATAC1);
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_INT(0, freedv_get_noise_weight(f));
    float p[64];
    TEST_ASSERT_EQUAL_INT(0, freedv_get_noise_profile(f, p, 64));
    freedv_set_noise_weight(f, 1);
    TEST_ASSERT_EQUAL_INT(1, freedv_get_noise_weight(f));
    freedv_set_noise_weight(f, 0);
    TEST_ASSERT_EQUAL_INT(0, freedv_get_noise_weight(f));
    freedv_close(f);
}

/* White noise: no carrier stands 2x over the median (the threshold of the
 * weights), so nothing would be weighted.  With the tone: the profile peaks
 * on one of the two carriers next to it, far above the median. */
static void test_profile_flat_then_peaked(void)
{
    int mode = FREEDV_MODE_DATAC1;
    struct freedv *tx = freedv_open(mode), *rx = open_rx(mode, 1);
    freedv_set_frames_per_burst(tx, 1);
    int nbytes = freedv_get_bits_per_modem_frame(tx) / 8;
    int cap = 4 * freedv_get_n_tx_modem_samples(tx) + 16000;
    short *buf = malloc(sizeof(short) * (size_t)cap);
    unsigned char payload[2048];
    float p[64];
    rng_s = 20260711ULL;

    int total = make_trial(tx, 15.0, 0.0, -99.0, buf, payload);
    TEST_ASSERT_TRUE(run_rx(rx, buf, total, payload, nbytes, NULL));
    int nc = freedv_get_noise_profile(rx, p, 64);
    TEST_ASSERT_EQUAL_INT(27, nc);
    float med = median_of(p, nc), mx = 0.0f;
    for (int c = 0; c < nc; c++) if (p[c] > mx) mx = p[c];
    TEST_ASSERT_TRUE_MESSAGE(med > 0.0f, "empty profile");
    TEST_ASSERT_TRUE_MESSAGE(mx < 2.0f * med, "white noise: a carrier stands out");

    total = make_trial(tx, 15.0, TONE_HZ, -3.0, buf, payload);
    (void)run_rx(rx, buf, total, payload, nbytes, NULL);
    nc = freedv_get_noise_profile(rx, p, 64);
    TEST_ASSERT_EQUAL_INT(27, nc);
    med = median_of(p, nc);
    int arg = 0;
    for (int c = 1; c < nc; c++) if (p[c] > p[arg]) arg = c;
    TEST_ASSERT_TRUE_MESSAGE(arg == TONE_C_LO || arg == TONE_C_HI, "peak is not next to the tone");
    TEST_ASSERT_TRUE_MESSAGE(p[arg] > 10.0f * med, "tone: the peak is not far above the median");

    free(buf);
    freedv_close(tx);
    freedv_close(rx);
}

typedef struct { int off, on, lost, on_path5, stock_path_mismatch; } tally_t;

static tally_t sweep(int mode, double snr3k, double tone_db, int trials, uint64_t seed)
{
    struct freedv *tx = freedv_open(mode), *rx0 = open_rx(mode, 0), *rx1 = open_rx(mode, 1);
    freedv_set_frames_per_burst(tx, 1);
    int nbytes = freedv_get_bits_per_modem_frame(tx) / 8;
    int cap = 4 * freedv_get_n_tx_modem_samples(tx) + 16000;
    short *buf = malloc(sizeof(short) * (size_t)cap);
    unsigned char payload[2048];
    tally_t t = {0};
    rng_s = seed;
    for (int i = 0; i < trials; i++) {
        int total = make_trial(tx, snr3k, TONE_HZ, tone_db, buf, payload);
        int p0 = -1, p1 = -1;
        int g0 = run_rx(rx0, buf, total, payload, nbytes, &p0);
        int g1 = run_rx(rx1, buf, total, payload, nbytes, &p1);
        t.off += g0;
        t.on += g1;
        if (g0 && !g1) t.lost++;
        if (g0 && g1 && p0 != p1) t.stock_path_mismatch++;
        if (g1 && p1 == 5) t.on_path5++;
    }
    free(buf);
    freedv_close(tx);
    freedv_close(rx0);
    freedv_close(rx1);
    return t;
}

/* DATAC1 at its white-noise cliff (about +1 dB), no interferer: 24 trials.
 * Whatever the stock receiver delivers the other delivers too, by the same
 * path, and the counts are equal. */
static void test_neutral_without_interference(void)
{
    tally_t t = sweep(FREEDV_MODE_DATAC1, 1.0, -99.0, 24, 7001ULL);
    printf("    neutral: off %d on %d of 24, lost %d, by path 5: %d\n", t.off, t.on, t.lost, t.on_path5);
    TEST_ASSERT_TRUE_MESSAGE(t.off > 2 && t.off < 24, "not at the cliff: the control says nothing");
    TEST_ASSERT_EQUAL_INT(0, t.lost);
    TEST_ASSERT_EQUAL_INT(0, t.stock_path_mismatch);
    TEST_ASSERT_EQUAL_INT(t.off, t.on);
}

/* A strong signal with no interferer never reaches the attempt. */
static void test_strong_signal_untouched(void)
{
    tally_t t = sweep(FREEDV_MODE_DATAC1, 12.0, -99.0, 6, 7002ULL);
    TEST_ASSERT_EQUAL_INT(6, t.off);
    TEST_ASSERT_EQUAL_INT(6, t.on);
    TEST_ASSERT_EQUAL_INT(0, t.on_path5);
    TEST_ASSERT_EQUAL_INT(0, t.stock_path_mismatch);
}

/* DATAC1 at +5 dB with a carrier at 1650 Hz, 3 dB below the signal: the stock
 * receiver delivers 0 of 20 and the weighted attempt 20 (seed 7003). */
static void test_tone_rescued(void)
{
    tally_t t = sweep(FREEDV_MODE_DATAC1, 5.0, -3.0, 20, 7003ULL);
    printf("    tone -3 dB: off %d on %d of 20, lost %d, by path 5: %d\n", t.off, t.on, t.lost, t.on_path5);
    TEST_ASSERT_TRUE_MESSAGE(t.off <= 2, "the stock receiver survives the tone: the case tests nothing");
    TEST_ASSERT_TRUE_MESSAGE(t.on >= 18, "the weighted attempt does not rescue the frames");
    TEST_ASSERT_EQUAL_INT(0, t.lost);
    TEST_ASSERT_TRUE(t.on_path5 >= t.on - t.off);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_off_by_default);
    RUN_TEST(test_profile_flat_then_peaked);
    RUN_TEST(test_neutral_without_interference);
    RUN_TEST(test_strong_signal_untouched);
    RUN_TEST(test_tone_rescued);
    return UNITY_END();
}
