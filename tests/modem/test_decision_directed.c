/* test_decision_directed -- decision-directed channel estimation as the last
 * decode attempt (freedv_set_decision_directed,
 * docs/improvements/20-decision-directed.md).
 *
 * What must hold:
 *  - it is off unless asked for;
 *  - the soft-symbol mapping is the inverse of the demapper: the symbol built
 *    from the soft bits of a constellation point demaps to those same bits,
 *    for QPSK and 16QAM (sign and bit-order errors hide here);
 *  - soft bits that say nothing give a symbol of no weight;
 *  - on, it never loses a frame the receiver without it delivers, and a
 *    frame the earlier attempts deliver never reaches it;
 *  - below the cliff of a receiver with proposals 09 and 19, fed the same
 *    samples, it adds frames, each through decode path 6.
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
#include "codec2_ofdm.h"
#include "comp.h"
#include "mpdecode_core.h"

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

static void test_off_by_default(void)
{
    struct freedv *f = freedv_open(FREEDV_MODE_DATAC1);
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_INT(0, freedv_get_decision_directed(f));
    freedv_set_decision_directed(f, 1);
    TEST_ASSERT_EQUAL_INT(1, freedv_get_decision_directed(f));
    freedv_set_decision_directed(f, 0);
    TEST_ASSERT_EQUAL_INT(0, freedv_get_decision_directed(f));
    freedv_close(f);
}

static void check_mapping(int bps)
{
    int M = 1 << bps;
    for (int i = 0; i < M; i++) {
        float llr[4], back[4];
        COMP s;
        float amp = 1.0f;
        for (int k = 0; k < bps; k++) llr[k] = (i >> (bps - 1 - k)) & 1 ? -20.0f : 20.0f;
        llrs_to_soft_symbols(&s, llr, bps, 1);
        float mag = sqrtf(s.real * s.real + s.imag * s.imag);
        TEST_ASSERT_TRUE_MESSAGE(mag > 0.4f, "a sure symbol came out with no weight");
        symbols_to_llrs(back, &s, &amp, 100.0f, 1.0f, bps, 1);
        for (int k = 0; k < bps; k++) {
            char msg[80];
            snprintf(msg, sizeof(msg), "bps %d point %d bit %d: sent %+.0f, demapped %+.1f", bps, i, k, llr[k], back[k]);
            TEST_ASSERT_TRUE_MESSAGE((llr[k] > 0) == (back[k] > 0), msg);
        }
    }
    /* no information: every point equally likely, so their mean, the origin */
    float zero[4] = {0, 0, 0, 0};
    COMP s;
    llrs_to_soft_symbols(&s, zero, bps, 1);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, s.real);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, s.imag);
}

static void test_soft_symbols_invert_the_demapper(void)
{
    check_mapping(2);
    check_mapping(4);
}

static int make_trial(struct freedv *tx, double snr3k, short *buf, unsigned char *payload)
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
    int lead = 3200, total = lead + n + n;
    for (int i = 0; i < total; i++) {
        double s = sigma * gauss();
        if (i >= lead && i < lead + n) s += b[i - lead] * g;
        if (s > 32767.0) s = 32767.0;
        if (s < -32768.0) s = -32768.0;
        buf[i] = (short)lrint(s);
    }
    free(b);
    return total;
}

static int run_rx(struct freedv *rx, const short *buf, int total, const unsigned char *payload, int nbytes, int *path)
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
        if (nb > 0 && !(freedv_get_rx_status(rx) & FREEDV_RX_BIT_ERRORS) && !memcmp(out, payload, (size_t)nbytes - 2)) {
            got = 1;
            if (path) *path = freedv_get_rx_decode_path(rx);
        }
    }
    return got;
}

typedef struct { int base, dd, lost, by_path6, path_changed; } tally_t;

/* The same samples to a receiver with proposals 09 and 19 and to one with
 * those and the decision-directed attempt. */
static tally_t sweep(int mode, double snr3k, int trials, uint64_t seed)
{
    struct freedv *tx = freedv_open(mode), *rx0 = freedv_open(mode), *rx1 = freedv_open(mode);
    TEST_ASSERT_NOT_NULL(tx); TEST_ASSERT_NOT_NULL(rx0); TEST_ASSERT_NOT_NULL(rx1);
    freedv_set_frames_per_burst(tx, 1);
    for (int k = 0; k < 2; k++) {
        struct freedv *rx = k ? rx1 : rx0;
        freedv_set_frames_per_burst(rx, 1);
        freedv_set_chanest_alt(rx, OFDM_CHANEST_T4F3);
        freedv_set_cp_recycle(rx, 4);
    }
    freedv_set_decision_directed(rx1, 1);
    int nbytes = freedv_get_bits_per_modem_frame(tx) / 8;
    int cap = 4 * freedv_get_n_tx_modem_samples(tx) + 16000;
    short *buf = malloc(sizeof(short) * (size_t)cap);
    unsigned char payload[2048];
    tally_t t = {0};
    rng_s = seed;
    for (int i = 0; i < trials; i++) {
        int total = make_trial(tx, snr3k, buf, payload);
        int p0 = -1, p1 = -1;
        int g0 = run_rx(rx0, buf, total, payload, nbytes, &p0);
        int g1 = run_rx(rx1, buf, total, payload, nbytes, &p1);
        t.base += g0;
        t.dd += g1;
        if (g0 && !g1) t.lost++;
        if (g0 && g1 && p0 != p1) t.path_changed++;
        if (g1 && p1 == 6) t.by_path6++;
    }
    free(buf);
    freedv_close(tx); freedv_close(rx0); freedv_close(rx1);
    return t;
}

/* DATAC1 at -1.5 dB, under the cliff of 09 + 19 (about -1.2 dB). */
static void test_adds_frames_below_the_cliff(void)
{
    tally_t t = sweep(FREEDV_MODE_DATAC1, -1.5, 24, 5150ULL);
    printf("    DATAC1 -1.5 dB: 09+19 delivers %d of 24, with the attempt %d (lost %d, by path 6: %d)\n", t.base, t.dd,
           t.lost, t.by_path6);
    TEST_ASSERT_EQUAL_INT(0, t.lost);
    TEST_ASSERT_EQUAL_INT(0, t.path_changed);
    /* 4 on x86-64; 2 leaves room for another platform's arithmetic */
    TEST_ASSERT_TRUE_MESSAGE(t.dd - t.base >= 2, "the attempt adds too few frames");
    TEST_ASSERT_EQUAL_INT(t.dd - t.base, t.by_path6);
}

/* Well above the cliff nothing reaches the attempt. */
static void test_strong_signal_untouched(void)
{
    tally_t t = sweep(FREEDV_MODE_DATAC1, 8.0, 5, 5151ULL);
    TEST_ASSERT_EQUAL_INT(5, t.base);
    TEST_ASSERT_EQUAL_INT(5, t.dd);
    TEST_ASSERT_EQUAL_INT(0, t.by_path6);
    TEST_ASSERT_EQUAL_INT(0, t.path_changed);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_off_by_default);
    RUN_TEST(test_soft_symbols_invert_the_demapper);
    RUN_TEST(test_adds_frames_below_the_cliff);
    RUN_TEST(test_strong_signal_untouched);
    return UNITY_END();
}
