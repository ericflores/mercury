/* test_tx_clip -- QAM16C2 transmit clipping behind a switch
 * (freedv_set_qam16c2_tx_clip, docs/improvements/02-qam16c2-tx-clip.md).
 *
 * What must hold:
 *  - off (the default), a QAM16C2 burst is sample for sample the burst of an
 *    instance that was never touched, also after switching on and off again;
 *  - on, the burst's peak-to-average ratio is the one the findings measured
 *    (6.67 dB, against 11.4 dB off), and its peak is no higher than the
 *    unclipped burst's, so a transmitter set for the old peak is not driven
 *    harder;
 *  - a clipped burst decodes in a stock receiver;
 *  - every other mode refuses the switch and its burst is unchanged.
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

void setUp(void) {}
void tearDown(void) {}

#define CAP 80000

/* The burst (preamble, packet, postamble) of a fixed payload. */
static int burst(struct freedv *tx, short *out)
{
    int nbytes = freedv_get_bits_per_modem_frame(tx) / 8;
    unsigned char payload[2048];
    for (int i = 0; i < nbytes - 2; i++) payload[i] = (unsigned char)(i * 37 + 11);
    uint16_t crc = freedv_gen_crc16(payload, nbytes - 2);
    payload[nbytes - 2] = (unsigned char)(crc >> 8);
    payload[nbytes - 1] = (unsigned char)(crc & 0xff);
    int n = freedv_rawdatapreambletx(tx, out);
    freedv_rawdatatx(tx, out + n, payload);
    n += freedv_get_n_tx_modem_samples(tx);
    n += freedv_rawdatapostambletx(tx, out + n);
    TEST_ASSERT_TRUE(n <= CAP);
    return n;
}

static void stats(const short *x, int n, double *papr_db, double *peak, double *rms)
{
    double ps = 0.0, pk = 0.0;
    for (int i = 0; i < n; i++) {
        ps += (double)x[i] * x[i];
        if (fabs((double)x[i]) > pk) pk = fabs((double)x[i]);
    }
    *rms = sqrt(ps / n);
    *peak = pk;
    *papr_db = 20.0 * log10(pk / *rms);
}

static struct freedv *open_tx(int mode)
{
    struct freedv *f = freedv_open(mode);
    TEST_ASSERT_NOT_NULL(f);
    freedv_set_frames_per_burst(f, 1);
    return f;
}

static void test_default_unchanged(void)
{
    static short a[CAP], b[CAP];
    struct freedv *ref = open_tx(FREEDV_MODE_QAM16C2), *f = open_tx(FREEDV_MODE_QAM16C2);
    int na = burst(ref, a);
    TEST_ASSERT_EQUAL_INT(1, freedv_set_qam16c2_tx_clip(f, 1));
    TEST_ASSERT_EQUAL_INT(1, freedv_set_qam16c2_tx_clip(f, 0));
    int nb = burst(f, b);
    TEST_ASSERT_EQUAL_INT(na, nb);
    TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof(short) * (size_t)na);
    freedv_close(ref);
    freedv_close(f);
}

static void test_clip_lowers_papr_not_raises_peak(void)
{
    static short a[CAP], b[CAP];
    struct freedv *off = open_tx(FREEDV_MODE_QAM16C2), *on = open_tx(FREEDV_MODE_QAM16C2);
    freedv_set_qam16c2_tx_clip(on, 1);
    int na = burst(off, a), nb = burst(on, b);
    double p0, k0, r0, p1, k1, r1;
    stats(a, na, &p0, &k0, &r0);
    stats(b, nb, &p1, &k1, &r1);
    printf("    off: PAPR %.2f dB, peak %.0f, rms %.0f;  on: PAPR %.2f dB, peak %.0f, rms %.0f (%+.2f dB average power)\n",
           p0, k0, r0, p1, k1, r1, 20.0 * log10(r1 / r0));
    TEST_ASSERT_EQUAL_INT(na, nb);
    TEST_ASSERT_TRUE_MESSAGE(p0 > 10.5, "the unclipped burst is not at the findings' 11.4 dB");
    TEST_ASSERT_TRUE_MESSAGE(p1 > 6.0 && p1 < 7.5, "the clipped burst is not at the findings' 6.67 dB");
    TEST_ASSERT_TRUE_MESSAGE(k1 <= k0 * 1.02, "clipping raised the peak: a radio set for the old peak would be overdriven");
    freedv_close(off);
    freedv_close(on);
}

static void test_clipped_decodes_in_a_stock_receiver(void)
{
    static short b[CAP + 16000];
    struct freedv *tx = open_tx(FREEDV_MODE_QAM16C2), *rx = open_tx(FREEDV_MODE_QAM16C2);
    freedv_set_qam16c2_tx_clip(tx, 1);
    memset(b, 0, sizeof(b));
    int n = burst(tx, b + 4000);
    int total = 4000 + n + 8000, pos = 0, got = 0;
    int nbytes = freedv_get_bits_per_modem_frame(rx) / 8;
    unsigned char out[2048];
    while (pos < total) {
        int nin = freedv_nin(rx);
        if (pos + nin > total) break;
        int nb = (int)freedv_rawdatarx(rx, out, b + pos);
        pos += nin;
        if (nb > 0 && !(freedv_get_rx_status(rx) & FREEDV_RX_BIT_ERRORS)) {
            got = 1;
            for (int i = 0; i < nbytes - 2; i++)
                TEST_ASSERT_EQUAL_UINT8((unsigned char)(i * 37 + 11), out[i]);
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(got, "a stock receiver did not decode the clipped burst");
    freedv_close(tx);
    freedv_close(rx);
}

static void test_other_modes_untouched(void)
{
    static short a[CAP], b[CAP];
    int modes[] = { FREEDV_MODE_DATAC17, FREEDV_MODE_DATAC1, FREEDV_MODE_DATAC16 };
    for (unsigned m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
        struct freedv *ref = open_tx(modes[m]), *f = open_tx(modes[m]);
        TEST_ASSERT_EQUAL_INT(0, freedv_set_qam16c2_tx_clip(f, 1));
        int na = burst(ref, a), nb = burst(f, b);
        TEST_ASSERT_EQUAL_INT(na, nb);
        TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof(short) * (size_t)na);
        freedv_close(ref);
        freedv_close(f);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_unchanged);
    RUN_TEST(test_clip_lowers_papr_not_raises_peak);
    RUN_TEST(test_clipped_decodes_in_a_stock_receiver);
    RUN_TEST(test_other_modes_untouched);
    return UNITY_END();
}
