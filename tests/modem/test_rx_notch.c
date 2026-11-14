/* test_rx_notch -- the automatic carrier notch (modem/rx_notch.c,
 * docs/improvements/07-noise-aware-llr.md, "Notch for strong carriers").
 *
 * What must hold:
 *  - off, the buffer is untouched; on with no carrier, it is untouched too,
 *    bit for bit;
 *  - a carrier in noise is found within 1.3 s, its frequency to 0.2 Hz, and
 *    is 40 dB down afterwards; two carriers get two notches;
 *  - a notch is released a few seconds after its carrier stops;
 *  - a carrier that drifts 1 Hz per second stays notched;
 *  - Mercury's own signals never engage it: back-to-back OFDM bursts of the
 *    control mode and of payload rungs, and tones that hop every 100-500 ms
 *    (what the MFSK floor and its patterns look like to this detector).
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
#include "rx_notch.h"

void setUp(void) {}
void tearDown(void) {}

#define FS 8000

static uint64_t rng_s = 1234567890123ULL;
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

static rx_notch_t g; /* 33 kB: not on the stack */

/* Amplitude of the component at f over x[from, to). */
static double tone_amp(const int16_t *x, int from, int to, double f)
{
    double cr = 0.0, ci = 0.0;
    for (int i = from; i < to; i++) {
        cr += x[i] * cos(2.0 * M_PI * f * i / FS);
        ci += x[i] * sin(2.0 * M_PI * f * i / FS);
    }
    return 2.0 * sqrt(cr * cr + ci * ci) / (to - from);
}

static void run(int16_t *x, int n)
{
    for (int pos = 0; pos < n; pos += 160) rx_notch_process(&g, x + pos, n - pos < 160 ? n - pos : 160);
}

static void test_off_and_idle_are_passthrough(void)
{
    enum { N = 5 * FS };
    static int16_t x[N], ref[N];
    for (int i = 0; i < N; i++) x[i] = ref[i] = clip16(2000.0 * gauss());
    rx_notch_init(&g, false, FS);
    run(x, N);
    TEST_ASSERT_EQUAL_MEMORY(ref, x, sizeof(x));
    rx_notch_init(&g, true, FS);
    run(x, N);
    TEST_ASSERT_EQUAL_UINT64(0, g.engaged);
    TEST_ASSERT_EQUAL_MEMORY(ref, x, sizeof(x));
}

static void check_carrier(double f, double tone_db)
{
    enum { N = 6 * FS };
    static int16_t x[N];
    double rms = 1000.0, ta = sqrt(2.0) * rms * pow(10.0, tone_db / 20.0);
    for (int i = 0; i < N; i++) x[i] = clip16(rms * gauss() + ta * cos(2.0 * M_PI * f * i / FS + 1.1));
    rx_notch_init(&g, true, FS);
    int first = -1;
    for (int pos = 0; pos < N; pos += 160)
        if (rx_notch_process(&g, x + pos, 160) && first < 0) first = pos;
    float fr[RX_NOTCH_MAX];
    int na = rx_notch_active(&g, fr, RX_NOTCH_MAX);
    double down = 20.0 * log10(ta / (tone_amp(x, N - 2 * FS, N, f) + 1e-9));
    char msg[160];
    snprintf(msg, sizeof(msg), "%.2f Hz at %+.0f dB: engaged at %.2f s, %d notch(es), at %.2f Hz, %.1f dB down", f,
             tone_db, first / (double)FS, na, na ? (double)fr[0] : 0.0, down);
    printf("    %s\n", msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, na, msg);
    TEST_ASSERT_TRUE_MESSAGE(first >= 0 && first <= (int)(1.3 * FS), msg);
    TEST_ASSERT_TRUE_MESSAGE(fabs(fr[0] - f) < 0.2, msg);
    TEST_ASSERT_TRUE_MESSAGE(down > 40.0, msg);
}

static void test_carrier_found_and_removed(void)
{
    check_carrier(1650.0, -3.0);
    check_carrier(1625.3, 10.0);
    check_carrier(700.7, -9.0);
    check_carrier(2300.0, 20.0);
}

static void test_two_carriers(void)
{
    enum { N = 5 * FS };
    static int16_t x[N];
    for (int i = 0; i < N; i++)
        x[i] = clip16(800.0 * gauss() + 1500.0 * cos(2.0 * M_PI * 1000.4 * i / FS) +
                      900.0 * cos(2.0 * M_PI * 1987.0 * i / FS + 2.0));
    rx_notch_init(&g, true, FS);
    run(x, N);
    float fr[RX_NOTCH_MAX];
    TEST_ASSERT_EQUAL_INT(2, rx_notch_active(&g, fr, RX_NOTCH_MAX));
    double lo = fr[0] < fr[1] ? fr[0] : fr[1], hi = fr[0] < fr[1] ? fr[1] : fr[0];
    TEST_ASSERT_TRUE(fabs(lo - 1000.4) < 0.2 && fabs(hi - 1987.0) < 0.2);
}

static void test_released_when_the_carrier_stops(void)
{
    enum { N = 9 * FS };
    static int16_t x[N];
    for (int i = 0; i < N; i++)
        x[i] = clip16(1000.0 * gauss() + (i < 3 * FS ? 2000.0 * cos(2.0 * M_PI * 1500.0 * i / FS) : 0.0));
    rx_notch_init(&g, true, FS);
    run(x, 3 * FS);
    TEST_ASSERT_EQUAL_INT(1, rx_notch_active(&g, NULL, 0));
    run(x + 3 * FS, 6 * FS);
    TEST_ASSERT_EQUAL_INT(0, rx_notch_active(&g, NULL, 0));
    TEST_ASSERT_EQUAL_UINT64(1, g.released);
}

/* 1 Hz per second for 8 s: the notch moves with the carrier. */
static void test_follows_a_drifting_carrier(void)
{
    enum { N = 10 * FS };
    static int16_t x[N];
    double ph = 0.0, ta = 2000.0;
    for (int i = 0; i < N; i++) {
        double f = 1400.0 + (i > 2 * FS ? (i - 2 * FS) / (double)FS : 0.0);
        ph += 2.0 * M_PI * f / FS;
        x[i] = clip16(700.0 * gauss() + ta * cos(ph));
    }
    static int16_t y[N];
    memcpy(y, x, sizeof(x));
    rx_notch_init(&g, true, FS);
    run(y, N);
    double pin = 0.0, pout = 0.0;
    for (int i = 8 * FS; i < N; i++) { pin += (double)x[i] * x[i]; pout += (double)y[i] * y[i]; }
    double noise = 700.0 * 700.0 * 2 * FS;
    printf("    drifting carrier: power in the last 2 s %.1f dB over the noise before, %.1f dB after\n",
           10.0 * log10(pin / noise), 10.0 * log10(pout / noise));
    TEST_ASSERT_TRUE(rx_notch_active(&g, NULL, 0) >= 1);
    /* what is left of the carrier is under the noise */
    TEST_ASSERT_TRUE(pout < 2.0 * noise);
}

/* Bursts of one mode back to back, payloads random, for about `seconds`. */
static int fill_bursts(int mode, int16_t *x, int cap, double peak)
{
    struct freedv *tx = freedv_open(mode);
    TEST_ASSERT_NOT_NULL(tx);
    freedv_set_frames_per_burst(tx, 1);
    int nbytes = freedv_get_bits_per_modem_frame(tx) / 8;
    int ntx = freedv_get_n_tx_modem_samples(tx);
    short *b = malloc(sizeof(short) * (size_t)(4 * ntx));
    int pos = 0;
    for (;;) {
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
        if (pos + n + 800 > cap) break;
        for (int i = 0; i < n; i++) x[pos + i] = clip16(b[i] * peak / pk + 30.0 * gauss());
        pos += n;
        for (int i = 0; i < 800; i++) x[pos + i] = clip16(30.0 * gauss()); /* 100 ms gap */
        pos += 800;
    }
    free(b);
    freedv_close(tx);
    return pos;
}

static void test_own_signals_never_engage(void)
{
    enum { CAP = 30 * FS };
    static int16_t x[CAP], ref[CAP];
    int modes[] = { FREEDV_MODE_DATAC16, FREEDV_MODE_DATAC15, FREEDV_MODE_DATAC4, FREEDV_MODE_DATAC3,
                    FREEDV_MODE_DATAC1, FREEDV_MODE_DATAC17, FREEDV_MODE_QAM16C2 };
    const char *names[] = { "DATAC16", "DATAC15", "DATAC4", "DATAC3", "DATAC1", "DATAC17", "QAM16C2" };
    for (unsigned m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
        int n = fill_bursts(modes[m], x, CAP, 12000.0);
        memcpy(ref, x, sizeof(int16_t) * (size_t)n);
        rx_notch_init(&g, true, FS);
        run(x, n);
        char msg[80];
        snprintf(msg, sizeof(msg), "%s: %.1f s of bursts engaged the notch", names[m], n / (double)FS);
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, g.engaged, msg);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(ref, x, sizeof(int16_t) * (size_t)n, msg);
    }
    /* hopping tones: one of 32 frequencies 31.25 Hz apart, for 100 to 500 ms each */
    int n = 20 * FS, left = 0;
    double f = 1000.0, ph = 0.0;
    for (int i = 0; i < n; i++) {
        if (!left) { f = 1000.0 + 31.25 * (int)(uni() * 32.0); left = (int)((0.1 + 0.4 * uni()) * FS); }
        left--;
        ph += 2.0 * M_PI * f / FS;
        x[i] = clip16(8000.0 * cos(ph) + 30.0 * gauss());
    }
    rx_notch_init(&g, true, FS);
    run(x, n);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, g.engaged, "hopping tones engaged the notch");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_off_and_idle_are_passthrough);
    RUN_TEST(test_carrier_found_and_removed);
    RUN_TEST(test_two_carriers);
    RUN_TEST(test_released_when_the_carrier_stops);
    RUN_TEST(test_follows_a_drifting_carrier);
    RUN_TEST(test_own_signals_never_engage);
    return UNITY_END();
}
