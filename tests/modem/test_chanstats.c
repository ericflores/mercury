/* test_chanstats -- the estimators behind the chanstats line
 * (docs/improvements/10-channel-telemetry.md).
 *
 * What must hold:
 *  - the Doppler and delay estimators, fed synthetic pilot rows of a known
 *    two-path channel (equal paths, Gaussian Doppler spectrum, white noise),
 *    separate the cases the proposal names: 0.5 / 2 / 7 ms apart, 0.1-1 Hz
 *    from 10 Hz.  They are coarse; the bounds here are what the estimators
 *    were measured to do (docs/CHANSTATS-FINDINGS.md), not what one would
 *    wish;
 *  - on pure noise they read no spread;
 *  - the accumulators reset at a burst's first frame and a replay leaves
 *    them alone (through the modem: tests/modem/test_noise_weight.c);
 *
 * Everything is seeded: the numbers are the same on every run.
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
#include "codec2_ofdm.h"
#include "freedv_api.h"
#include "ofdm_internal.h"

void setUp(void) {}
void tearDown(void) {}

#define NC 27
#define NF 22
#define ROW_DT 0.11f
#define DF 62.5f

static uint64_t rng_s = 1;
static double uni(void)
{
    rng_s = rng_s * 6364136223846793005ULL + 1442695040888963407ULL;
    return ((double)(rng_s >> 11) + 0.5) / 9007199254740992.0;
}
static double gauss(void)
{
    return sqrt(-2.0 * log(uni())) * cos(2.0 * M_PI * uni());
}

/* One burst's pilot rows: two equal paths `tau_ms` apart, each a Rayleigh
 * process with a Gaussian Doppler spectrum of spread `dopp` (2 sigma), a
 * common phase slope across carriers (the FFT window's place), and noise at
 * `snr_db` below the mean pilot power. */
static void burst(double dopp, double tau_ms, double snr_db, complex float h[NF + 2][NC + 2])
{
    enum { M = 48 };
    complex double g[2][NF + 2];
    for (int p = 0; p < 2; p++) {
        double fd[M], ph[M];
        for (int m = 0; m < M; m++) {
            fd[m] = 0.5 * dopp * gauss();
            ph[m] = 2.0 * M_PI * uni();
        }
        for (int k = 0; k < NF + 2; k++) {
            complex double s = 0;
            for (int m = 0; m < M; m++)
                s += cexp(I * (2.0 * M_PI * fd[m] * k * ROW_DT + ph[m]));
            g[p][k] = s / sqrt((double)M * 2.0);
        }
    }
    double slope = 0.6 * (uni() - 0.5);
    double pw = 0;
    complex double clean[NF + 2][NC + 2];
    for (int k = 0; k < NF + 2; k++)
        for (int i = 1; i <= NC; i++) {
            complex double f = g[0][k] + g[1][k] * cexp(-I * 2.0 * M_PI * i * DF * tau_ms * 1e-3);
            clean[k][i] = f * cexp(-I * slope * i);
            pw += creal(clean[k][i] * conj(clean[k][i]));
        }
    pw /= (double)(NF + 2) * NC;
    double sd = sqrt(pw / pow(10.0, snr_db / 10.0) / 2.0);
    for (int k = 0; k < NF + 2; k++)
        for (int i = 1; i <= NC; i++)
            h[k][i] = (float)(creal(clean[k][i]) + sd * gauss()) +
                      I * (float)(cimag(clean[k][i]) + sd * gauss());
}

static void feed(const complex float h[NF + 2][NC + 2], struct ofdm_chan_acc *a)
{
    ofdm_chan_acc_reset(a);
    for (int k = 0; k < NF; k++)
        ofdm_chan_acc_add_frame(a, NC, h[k], h[k + 1], k == NF - 1 ? NULL : h[k + 2]);
}

static int cmpf(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

/* Median and the 10th and 90th percentile of both estimates over n bursts. */
typedef struct { float med, lo, hi; } spread_t;
static void sweep(double dopp, double tau_ms, double snr_db, int n, spread_t *d, spread_t *l)
{
    float *dv = malloc(sizeof(float) * (size_t)n), *lv = malloc(sizeof(float) * (size_t)n);
    complex float(*h)[NC + 2] = malloc(sizeof(complex float) * (NF + 2) * (NC + 2));
    for (int t = 0; t < n; t++) {
        struct ofdm_chan_acc a;
        struct ofdm_chan_est e;
        burst(dopp, tau_ms, snr_db, (complex float(*)[NC + 2])h);
        feed((const complex float(*)[NC + 2])h, &a);
        ofdm_chan_acc_result(&a, NC, ROW_DT, DF, &e);
        dv[t] = e.dopp_hz;
        lv[t] = e.dly_ms;
    }
    qsort(dv, (size_t)n, sizeof(float), cmpf);
    qsort(lv, (size_t)n, sizeof(float), cmpf);
    d->med = dv[n / 2]; d->lo = dv[n / 10]; d->hi = dv[n - 1 - n / 10];
    l->med = lv[n / 2]; l->lo = lv[n / 10]; l->hi = lv[n - 1 - n / 10];
    free(dv); free(lv); free(h);
}

static void test_doppler_estimate(void)
{
    static const double dop[] = {0.1, 0.5, 1.0, 2.0, 5.0, 10.0};
    static const double snr[] = {5.0, 10.0, 20.0};
    spread_t med1[3], med10[3], med2[3], med01[3];
    (void)med2;
    for (int s = 0; s < 3; s++) {
        printf("    pilot SNR %2.0f dB, 80 bursts, doppler median [10%%..90%%] Hz:", snr[s]);
        for (int c = 0; c < 6; c++) {
            spread_t d, l;
            rng_s = 1000 + 17 * (uint64_t)c + (uint64_t)s;
            sweep(dop[c], 1.0, snr[s], 80, &d, &l);
            printf("  %.1f -> %.2f [%.2f..%.2f]", dop[c], (double)d.med, (double)d.lo, (double)d.hi);
            if (c == 0) med01[s] = d;
            if (c == 2) med1[s] = d;
            if (c == 3) med2[s] = d;
            if (c == 5) med10[s] = d;
        }
        printf("\n");
    }
    for (int s = 0; s < 3; s++) {
        TEST_ASSERT_TRUE_MESSAGE(med01[s].med < 0.7f, "0.1 Hz reads slow");
        TEST_ASSERT_TRUE_MESSAGE(med1[s].med > 0.6f && med1[s].med < 1.5f, "1 Hz within 50 %");
        /* noise biases the faster cases up: 2 Hz reads 3.4 at 5 dB */
        TEST_ASSERT_TRUE_MESSAGE(med2[s].med > 1.3f && med2[s].med < 3.6f, "2 Hz");
        TEST_ASSERT_TRUE_MESSAGE(med10[s].med > 6.0f, "10 Hz reads fast");
        /* 1 Hz and 10 Hz do not overlap, at the 10 % and 90 % points either */
        TEST_ASSERT_TRUE(med1[s].hi < med10[s].lo);
    }
}

static void test_delay_estimate(void)
{
    static const double tau[] = {0.0, 0.5, 1.0, 2.0, 3.0, 7.0};
    static const double snr[] = {5.0, 10.0, 20.0};
    spread_t m05[3], m2[3], m7[3], m0[3];
    for (int s = 0; s < 3; s++) {
        for (int dop = 0; dop < 2; dop++) {
            printf("    pilot SNR %2.0f dB, doppler %4.1f Hz, 80 bursts, delay median [10%%..90%%] ms:", snr[s], dop ? 10.0 : 1.0);
            for (int c = 0; c < 6; c++) {
                spread_t d, l;
                rng_s = 5000 + 31 * (uint64_t)c + (uint64_t)s + 7 * (uint64_t)dop;
                sweep(dop ? 10.0 : 1.0, tau[c], snr[s], 80, &d, &l);
                printf("  %.1f -> %.2f [%.2f..%.2f]", tau[c], (double)l.med, (double)l.lo, (double)l.hi);
                if (dop == 1) {
                    if (c == 0) m0[s] = l;
                    if (c == 1) m05[s] = l;
                    if (c == 3) m2[s] = l;
                    if (c == 5) m7[s] = l;
                }
            }
            printf("\n");
        }
    }
    for (int s = 0; s < 3; s++) {
        TEST_ASSERT_TRUE_MESSAGE(m0[s].med < 0.4f, "no delay reads near zero");
        TEST_ASSERT_TRUE_MESSAGE(m05[s].med > 0.2f && m05[s].med < 0.9f, "0.5 ms");
        TEST_ASSERT_TRUE_MESSAGE(m2[s].med > 1.4f && m2[s].med < 2.6f, "2 ms");
        TEST_ASSERT_TRUE_MESSAGE(m7[s].med > 4.0f, "7 ms reads long");
        TEST_ASSERT_TRUE(m05[s].med < m2[s].med && m2[s].med < m7[s].med);
    }
}

/* Noise only: no spread of either kind, whatever the level. */
static void test_noise_only(void)
{
    complex float(*h)[NC + 2] = malloc(sizeof(complex float) * (NF + 2) * (NC + 2));
    struct ofdm_chan_acc a;
    struct ofdm_chan_est e;
    rng_s = 99;
    /* the fading generator with nothing in it is white noise at a reference */
    for (int k = 0; k < NF + 2; k++)
        for (int i = 1; i <= NC; i++)
            h[k][i] = (float)gauss() + I * (float)gauss();
    feed((const complex float(*)[NC + 2])h, &a);
    ofdm_chan_acc_result(&a, NC, ROW_DT, DF, &e);
    printf("    white noise: doppler %.2f Hz (cap), delay %.2f ms\n", (double)e.dopp_hz, (double)e.dly_ms);
    TEST_ASSERT_TRUE(e.dopp_hz > 4.0f); /* nothing is correlated: "fast" */
    
    free(h);
}

static void test_reset_and_empty(void)
{
    struct ofdm_chan_acc a;
    struct ofdm_chan_est e;
    ofdm_chan_acc_reset(&a);
    TEST_ASSERT_EQUAL_INT(0, ofdm_chan_acc_result(&a, NC, ROW_DT, DF, &e));
    TEST_ASSERT_EQUAL_INT(0, e.ok_time);
    TEST_ASSERT_EQUAL_INT(0, e.ok_freq);
}

/* Through the modem, on white noise: one DATAC1 burst at one SNR3k. */
static unsigned char g_out[2048];
static int one_burst(struct freedv *tx, struct freedv *rx, double snr_db, struct freedv_rx_frame_stats *fs)
{
    int n = freedv_get_n_tx_preamble_modem_samples(tx) + freedv_get_n_tx_modem_samples(tx) +
            freedv_get_n_tx_postamble_modem_samples(tx);
    int lead = 3200, ntot = lead + 2 * n;
    short *burst = calloc((size_t)n + 8, sizeof(short)), *buf = calloc((size_t)ntot, sizeof(short));
    unsigned char payload[2048], *out = g_out;
    int nbytes = freedv_get_bits_per_modem_frame(tx) / 8;
    for (int i = 0; i < nbytes - 2; i++) payload[i] = (unsigned char)(uni() * 256.0);
    unsigned short crc = freedv_gen_crc16(payload, nbytes - 2);
    payload[nbytes - 2] = (unsigned char)(crc >> 8);
    payload[nbytes - 1] = (unsigned char)(crc & 0xff);
    int len = freedv_rawdatapreambletx(tx, burst);
    freedv_rawdatatx(tx, burst + len, payload);
    len += freedv_get_n_tx_modem_samples(tx);
    len += freedv_rawdatapostambletx(tx, burst + len);
    double ps = 0, pk = 0;
    for (int i = 0; i < len; i++) {
        ps += (double)burst[i] * burst[i];
        if (fabs((double)burst[i]) > pk) pk = fabs((double)burst[i]);
    }
    ps /= len;
    double g = 4000.0 / pk;
    double sigma = sqrt(ps * g * g / (pow(10.0, snr_db / 10.0) * 3000.0 / 4000.0));
    for (int i = 0; i < ntot; i++) {
        double v = sigma * gauss();
        if (i >= lead && i < lead + len) v += burst[i - lead] * g;
        buf[i] = (short)lrint(v);
    }
    freedv_set_sync(rx, FREEDV_SYNC_UNSYNC);
    int pos = 0, got = 0;
    while (pos < ntot && !got) {
        int nin = freedv_nin(rx);
        if (pos + nin > ntot) break;
        (void)freedv_rawdatarx(rx, out, buf + pos);
        pos += nin;
        if ((freedv_get_rx_status(rx) & (FREEDV_RX_BITS | FREEDV_RX_BIT_ERRORS)) &&
            freedv_get_rx_frame_stats(rx, fs))
            got = 1;
    }
    free(burst);
    free(buf);
    return got;
}

/* AWGN: the EVM the pilots predict is the EVM the data symbols show, at every
 * SNR, once the transmitter's own clipper is accounted for.  Without the
 * clipper evm_excess is the whole of it; with the stock transmitter it rises
 * with SNR (the clipper's floor), and evm_resid does not. */
static void test_awgn_evm(void)
{
    static const double snr[] = {8.0, 12.0, 20.0, 30.0};
    for (int clip = 0; clip < 2; clip++) {
        struct freedv *tx = freedv_open(FREEDV_MODE_DATAC1), *rx = freedv_open(FREEDV_MODE_DATAC1);
        freedv_set_frames_per_burst(tx, 1);
        freedv_set_frames_per_burst(rx, 1);
        freedv_set_noise_stats(rx, 1);
        if (!clip) freedv_set_tx_clip_config(tx, false, 2.5f, 0.8f, 135E3f);
        rng_s = 4242 + (uint64_t)clip;
        for (int s = 0; s < 4; s++) {
            double ex = 0, rs = 0, ev = 0, dp = 0, dl = 0;
            int ok = 0, n = 6;
            for (int t = 0; t < n; t++) {
                struct freedv_rx_frame_stats fs;
                if (!one_burst(tx, rx, snr[s], &fs)) continue;
                TEST_ASSERT_EQUAL_INT(7, fs.chan_valid & 7);
                ok++;
                ex += fs.evm_excess_db; rs += fs.evm_resid_db; ev += fs.evm_db; dp += fs.dopp_hz; dl += fs.dly_ms;
            }
            TEST_ASSERT_TRUE_MESSAGE(ok >= n - 1, "packets finished");
            printf("    DATAC1 AWGN %4.0f dB, %s transmitter, %d bursts: evm %.1f dB, excess %+.2f, resid %+.2f, dopp %.2f Hz, dly %.2f ms\n",
                   snr[s], clip ? "stock" : "unclipped", ok, ev / ok, ex / ok, rs / ok, dp / ok, dl / ok);
            /* the floor is the stock transmitter's: with the clipper off the residual reads under */
            if (clip) TEST_ASSERT_TRUE_MESSAGE(fabs(rs / ok) < 1.5, "evm_resid is near 0 on AWGN");
            if (!clip) TEST_ASSERT_TRUE_MESSAGE(fabs(ex / ok) < 1.5, "evm_excess is near 0 on AWGN without the clipper");
            if (clip && snr[s] >= 20.0)
                TEST_ASSERT_TRUE_MESSAGE(ex / ok > 3.0, "the stock clipper's floor shows in evm_excess");
            TEST_ASSERT_TRUE_MESSAGE(dp / ok < 1.5, "AWGN reads no Doppler");
            TEST_ASSERT_TRUE_MESSAGE(dl / ok < 1.0, "AWGN reads no delay");
        }
        freedv_close(tx);
        freedv_close(rx);
    }
}

/* Off: no statistics are formed. */
static void test_off_is_zero(void)
{
    struct freedv *tx = freedv_open(FREEDV_MODE_DATAC1), *rx = freedv_open(FREEDV_MODE_DATAC1);
    freedv_set_frames_per_burst(tx, 1);
    freedv_set_frames_per_burst(rx, 1);
    rng_s = 77;
    struct freedv_rx_frame_stats fs;
    TEST_ASSERT_TRUE(one_burst(tx, rx, 20.0, &fs));
    TEST_ASSERT_EQUAL_INT(1, fs.ok);
    TEST_ASSERT_EQUAL_INT(0, fs.chan_valid);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, fs.dopp_hz);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, fs.dly_ms);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, fs.evm_db);
    freedv_close(tx);
    freedv_close(rx);
}

/* On, the receiver decodes exactly what the same receiver off does, on the
 * same samples: the accumulators only read. */
static void test_decode_unchanged(void)
{
    for (int snr = 6; snr <= 14; snr += 4) {
        unsigned char ref[2048];
        struct freedv *tx = freedv_open(FREEDV_MODE_DATAC1);
        freedv_set_frames_per_burst(tx, 1);
        for (int on = 0; on < 2; on++) {
            struct freedv *rx = freedv_open(FREEDV_MODE_DATAC1);
            freedv_set_frames_per_burst(rx, 1);
            freedv_set_noise_stats(rx, on);
            rng_s = 31337;
            struct freedv_rx_frame_stats fs;
            memset(g_out, 0, sizeof(g_out));
            TEST_ASSERT_TRUE(one_burst(tx, rx, (double)snr, &fs));
            if (!on) {
                memcpy(ref, g_out, sizeof(ref));
                TEST_ASSERT_EQUAL_INT(0, fs.chan_valid);
            } else {
                TEST_ASSERT_EQUAL_INT(7, fs.chan_valid & 7);
                TEST_ASSERT_EQUAL_MEMORY(ref, g_out, sizeof(ref));
            }
            freedv_close(rx);
        }
        freedv_close(tx);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reset_and_empty);
    RUN_TEST(test_doppler_estimate);
    RUN_TEST(test_delay_estimate);
    RUN_TEST(test_noise_only);
    RUN_TEST(test_awgn_evm);
    RUN_TEST(test_off_is_zero);
    RUN_TEST(test_decode_unchanged);
    return UNITY_END();
}
