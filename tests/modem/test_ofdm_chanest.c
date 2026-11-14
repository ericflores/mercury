/* test_ofdm_chanest -- the alternative channel estimate as a second decode
 * attempt (freedv_set_chanest_alt, docs/improvements/09).
 *
 * What must hold:
 *  - it is off unless asked for;
 *  - on, it never loses a frame the stock decode delivers (the stock attempt
 *    runs first and unchanged), and a frame it adds is the frame that was
 *    sent;
 *  - at a mode's cliff it adds frames: the point of it.
 *
 * Both receivers of a trial are fed the same samples; noise is a seeded
 * generator, so the counts are the same on every run.
 *
 * Copyright (C) 2026 Rhizomatica
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "freedv_api.h"
#include "codec2_ofdm.h"

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

/* One burst of `mode` at snr3k dB in AWGN, with noise before and after, in
 * buf; the payload sent in payload.  Returns the sample count. */
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

typedef struct { int stock, alt, alt_only, lost, by_alt_path; } tally_t;

static tally_t sweep(int mode, int chanest, double snr3k, int trials, uint64_t seed)
{
    struct freedv *tx = freedv_open(mode), *rx0 = freedv_open(mode), *rx1 = freedv_open(mode);
    TEST_ASSERT_NOT_NULL(tx); TEST_ASSERT_NOT_NULL(rx0); TEST_ASSERT_NOT_NULL(rx1);
    freedv_set_frames_per_burst(tx, 1);
    freedv_set_frames_per_burst(rx0, 1);
    freedv_set_frames_per_burst(rx1, 1);
    freedv_set_chanest_alt(rx1, chanest);
    TEST_ASSERT_EQUAL_INT(chanest, freedv_get_chanest_alt(rx1));
    int nbytes = freedv_get_bits_per_modem_frame(tx) / 8;
    int ntx = freedv_get_n_tx_modem_samples(tx);
    short *buf = malloc(sizeof(short) * (size_t)(3200 + 8 * ntx));
    unsigned char payload[2048];
    tally_t t = {0};
    rng_s = seed;
    for (int k = 0; k < trials; k++) {
        int total = make_trial(tx, snr3k, buf, payload);
        int path = -1;
        int a = run_rx(rx0, buf, total, payload, nbytes, NULL);
        int b = run_rx(rx1, buf, total, payload, nbytes, &path);
        t.stock += a; t.alt += b;
        if (b && !a) t.alt_only++;
        if (a && !b) t.lost++;
        if (b && path == 2) t.by_alt_path++;
    }
    free(buf);
    freedv_close(tx); freedv_close(rx0); freedv_close(rx1);
    return t;
}

static void test_off_by_default(void)
{
    struct freedv *f = freedv_open(FREEDV_MODE_DATAC3);
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_INT(OFDM_CHANEST_OFF, freedv_get_chanest_alt(f));
    freedv_set_chanest_alt(f, 99);                      /* out of range: off */
    TEST_ASSERT_EQUAL_INT(OFDM_CHANEST_OFF, freedv_get_chanest_alt(f));
    freedv_close(f);
}

/* Well above the cliff every frame decodes on the stock attempt: the second
 * attempt is never reached, and nothing changes. */
static void test_strong_signal_uses_stock_path(void)
{
    tally_t t = sweep(FREEDV_MODE_DATAC3, OFDM_CHANEST_T4F3, 5.0, 6, 11);
    TEST_ASSERT_EQUAL_INT(6, t.stock);
    TEST_ASSERT_EQUAL_INT(6, t.alt);
    TEST_ASSERT_EQUAL_INT(0, t.by_alt_path);
}

/* At the cliff: no frame of the stock decode is lost, and frames are added,
 * each through the second attempt and each the frame that was sent (run_rx
 * compares the bytes). */
static void check_cliff(int mode, int chanest, double snr3k, int trials, int min_added)
{
    tally_t t = sweep(mode, chanest, snr3k, trials, 2026);
    char msg[160];
    snprintf(msg, sizeof msg, "mode %d chanest %d at %.1f dB: stock %d, with alt %d, added %d, lost %d",
             mode, chanest, snr3k, t.stock, t.alt, t.alt_only, t.lost);
    TEST_MESSAGE(msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, t.lost, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(t.alt - t.stock, t.alt_only, msg);
    TEST_ASSERT_TRUE_MESSAGE(t.alt_only >= min_added, msg);
    TEST_ASSERT_TRUE_MESSAGE(t.by_alt_path >= t.alt_only, msg);
}

static void test_datac3_cliff_t4f3(void) { check_cliff(FREEDV_MODE_DATAC3, OFDM_CHANEST_T4F3, -5.0, 16, 4); }
static void test_datac3_cliff_t4(void)   { check_cliff(FREEDV_MODE_DATAC3, OFDM_CHANEST_T4, -5.0, 16, 1); }
static void test_datac3_cliff_t4f5(void) { check_cliff(FREEDV_MODE_DATAC3, OFDM_CHANEST_T4F5, -5.0, 16, 4); }
/* Three carriers: time averaging only. */
static void test_datac15_cliff_t4(void)  { check_cliff(FREEDV_MODE_DATAC15, OFDM_CHANEST_T4, -11.0, 16, 2); }

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_off_by_default);
    RUN_TEST(test_strong_signal_uses_stock_path);
    RUN_TEST(test_datac3_cliff_t4f3);
    RUN_TEST(test_datac3_cliff_t4);
    RUN_TEST(test_datac3_cliff_t4f5);
    RUN_TEST(test_datac15_cliff_t4);
    return UNITY_END();
}
