/* test_ofdm_profile -- window placement from the pilots' delay profile, as a
 * replay of a burst whose earlier decode attempts failed
 * (freedv_set_profile_track, docs/improvements/26-window-placement.md).
 *
 * What must hold:
 *  - it is off unless asked for, and modes with fewer than 9 carriers refuse;
 *  - path detection on synthetic pilot rows: one path at a known lag, two
 *    paths 56 samples apart with either one the stronger, a path that
 *    disappears for 10 frames keeps its track and its place, noise only
 *    yields (almost) no path;
 *  - on a clean DATAC1 burst the detector reports one path at about -42 (the
 *    stock window's place) and the chosen shift is 0;
 *  - a replay with the tracker disabled (stock timing, test-only) reproduces
 *    the symbols of the live demodulator to within rounding: the rewind
 *    arithmetic is right (written before the tracker is wired in);
 *  - the live demodulator is untouched by the feature: its tracking fields
 *    and the symbol buffers of the stock, 09 and 19 paths are bit-identical
 *    after every call to those of a receiver with it off;
 *  - a strong clean signal never reaches the new attempt;
 *  - on a two-path channel the attempt adds frames and loses none.
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

/* One burst of `mode` at snr3k dB in AWGN (snr3k > 90: no noise), with noise
 * before and after, in buf; the payload sent in payload.  When echo_amp > 0 a
 * copy of the burst delayed by echo_delay samples at that amplitude is added
 * before the noise; the SNR is that of the direct path.  With alt_period > 0
 * the two paths' powers swap smoothly with that period in samples (the early
 * path's gain goes 1.0 -> 0.25 -> 1.0 while the late one's goes the other
 * way, times echo_amp): the dominant path changes, as on disturbed NVIS,
 * which is what loses the stock timing tracker.  Returns the sample count. */
static int alt_period = 0;
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
        double ge = 1.0, gl = echo_amp;
        if (alt_period > 0 && echo_amp > 0.0) {
            double w = 0.5 * (1.0 + cos(2.0 * M_PI * (double)(i - lead) / (double)alt_period));
            ge = 0.25 + 0.75 * w;
            gl = echo_amp * (0.25 + 0.75 * (1.0 - w));
        }
        if (i >= lead && i < lead + n) s += ge * b[i - lead] * g;
        if (echo_amp > 0.0 && i - echo_delay >= lead && i - echo_delay < lead + n)
            s += gl * b[i - echo_delay - lead] * g;
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

/* ------------------------------------------------------------------ switch */

static void test_off_by_default_and_few_carriers_refuse(void)
{
    struct freedv *f = freedv_open(FREEDV_MODE_DATAC1);
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_INT(0, freedv_get_profile_track(f));
    freedv_set_profile_track(f, 1);
    TEST_ASSERT_EQUAL_INT(1, freedv_get_profile_track(f));
    freedv_set_profile_track(f, 0);
    TEST_ASSERT_EQUAL_INT(0, freedv_get_profile_track(f));
    freedv_close(f);

    f = freedv_open(FREEDV_MODE_DATAC3);                 /* 9 carriers: the least */
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_INT(9, f->ofdm->nc);
    freedv_set_profile_track(f, 1);
    TEST_ASSERT_EQUAL_INT(1, freedv_get_profile_track(f));
    freedv_close(f);

    f = freedv_open(FREEDV_MODE_DATAC15);                /* 3 carriers */
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_TRUE(f->ofdm->nc < 9);
    freedv_set_profile_track(f, 1);
    TEST_ASSERT_EQUAL_INT(0, freedv_get_profile_track(f));
    freedv_close(f);

    f = freedv_open(FREEDV_MODE_DATAC4);                 /* 4 carriers */
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_TRUE(f->ofdm->nc < 9);
    freedv_set_profile_track(f, 1);
    TEST_ASSERT_EQUAL_INT(0, freedv_get_profile_track(f));
    freedv_close(f);
}

/* ------------------------------------------------------- path detection */

/* nrows pilot rows seen through a window, each path p with lag L_p and a
 * complex gain that changes from row to row (rows are a modem frame apart),
 * plus complex noise: H[r*nc+i] = sum_p g_pr exp(+j 2 pi (i+1) L_p / m). */
static void make_rows(const struct OFDM *o, int nrows, int npaths,
                      const int *lag, const double *amp, double sigma,
                      complex float *H)
{
    int nc = o->nc, m = o->m;
    for (int r = 0; r < nrows; r++) {
        complex double g[4];
        for (int p = 0; p < npaths; p++) {
            double ph = 2.0 * M_PI * uni();
            g[p] = amp[p] * (cos(ph) + I * sin(ph));
        }
        for (int i = 0; i < nc; i++) {
            complex double h = 0.0;
            for (int p = 0; p < npaths; p++) {
                double a = 2.0 * M_PI * (double)(i + 1) * (double)lag[p] / (double)m;
                h += g[p] * (cos(a) + I * sin(a));
            }
            if (sigma > 0.0) h += sigma * (gauss() + I * gauss()) / sqrt(2.0);
            H[r * nc + i] = (complex float)h;
        }
    }
}

/* Lags as the next frame's rows show them after the placement moved the
 * window from w0 to ofdm->pt_w: they move with it (lag = window start minus
 * the path's start), modulo one symbol. */
static void follow_window(const struct OFDM *o, int w0, int n, const int *base, int *lag)
{
    for (int p = 0; p < n; p++) {
        int l = base[p] + (o->pt_w - w0);
        while (l < -o->m / 2) l += o->m;
        while (l >= o->m / 2) l -= o->m;
        lag[p] = l;
    }
}

static struct freedv *open_pt(int mode)
{
    struct freedv *f = freedv_open(mode);
    TEST_ASSERT_NOT_NULL(f);
    freedv_set_profile_track(f, 1);
    TEST_ASSERT_EQUAL_INT(1, freedv_get_profile_track(f));
    return f;
}

static void test_detect_one_path(void)
{
    struct freedv *f = open_pt(FREEDV_MODE_DATAC1);
    struct OFDM *o = f->ofdm;
    complex float H[4 * 32];
    rng_s = 7;
    int lags[] = {-42, -30, 0, 17, -63, 63, -64};
    for (unsigned k = 0; k < sizeof lags / sizeof lags[0]; k++) {
        int lag[OFDM_PT_MAXPATHS];
        float pw[OFDM_PT_MAXPATHS], ratio;
        double amp[1] = {1.0};
        make_rows(o, 4, 1, &lags[k], amp, 0.1, H);
        int n = ofdm_profile_paths(o, H, 4, lag, pw, &ratio);
        char msg[80];
        snprintf(msg, sizeof msg, "lag %d: found %d, first %d", lags[k], n, n ? lag[0] : 0);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, n, msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(lags[k], lag[0], msg);
    }
    freedv_close(f);
}

/* Two paths 56 samples apart (7 ms is 56 samples), either the stronger: both
 * are found, and the placement keeps the stronger one inside the prefix. */
static void check_two_paths(double a_early, double a_late)
{
    struct freedv *f = open_pt(FREEDV_MODE_DATAC1);
    struct OFDM *o = f->ofdm;
    complex float H[4 * 32];
    int base2[2] = {-58, -2}, lag2[2];
    double amp[2] = {a_early, a_late};
    rng_s = 11;
    ofdm_profile_reset(o, 0);
    for (int frame = 0; frame < 6; frame++) {
        int lag[OFDM_PT_MAXPATHS];
        float pw[OFDM_PT_MAXPATHS], ratio;
        follow_window(o, 0, 2, base2, lag2);
        make_rows(o, 4, 2, lag2, amp, 0.05, H);
        int n = ofdm_profile_paths(o, H, 4, lag, pw, &ratio);
        char msg[100];
        snprintf(msg, sizeof msg, "frame %d, amplitudes %.1f %.1f: found %d", frame, a_early, a_late, n);
        TEST_ASSERT_EQUAL_INT_MESSAGE(2, n, msg);
        int d = abs(lag[0] - lag[1]);
        if (d > 64) d = 128 - d;
        TEST_ASSERT_EQUAL_INT_MESSAGE(56, d, msg);
        /* the strongest comes first */
        TEST_ASSERT_TRUE_MESSAGE(lag[0] == (a_early > a_late ? lag2[0] : lag2[1]), msg);
        ofdm_profile_place(o, n, lag, pw, frame == 0);
    }
    TEST_ASSERT_EQUAL_INT(2, o->pt_ntrk);
    /* the stronger path's track lies where it leaves no other symbol in the
     * window: lag in [-ncp+2, -2] */
    int ts = o->pt_trkw[0] > o->pt_trkw[1] ? 0 : 1;
    char msg[100];
    snprintf(msg, sizeof msg, "stronger track at %.0f, weaker at %.0f", (double)o->pt_trk[ts], (double)o->pt_trk[1 - ts]);
    TEST_ASSERT_TRUE_MESSAGE(o->pt_trk[ts] >= -(o->ncp - 2) && o->pt_trk[ts] <= -2, msg);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1.0f, 56.0f, fabsf(o->pt_trk[0] - o->pt_trk[1]), msg);
    freedv_close(f);
}

static void test_detect_two_paths_early_stronger(void) { check_two_paths(1.0, 0.5); }
static void test_detect_two_paths_late_stronger(void)  { check_two_paths(0.5, 1.0); }

/* One frame of synthetic rows through detection and placement; the paths'
 * lags follow the window as the receiver would see them.  Returns the number
 * of paths found. */
static int step_frame(struct OFDM *o, int w0, int first, int npresent,
                      const int *base, const double *amp)
{
    complex float H[4 * 32];
    int lag2[2], lag[OFDM_PT_MAXPATHS];
    float pw[OFDM_PT_MAXPATHS], ratio;
    follow_window(o, w0, 2, base, lag2);
    make_rows(o, 4, npresent, lag2, amp, 0.05, H);
    int n = ofdm_profile_paths(o, H, 4, lag, pw, &ratio);
    ofdm_profile_place(o, n, lag, pw, first);
    return n;
}

/* The weaker path is gone for 10 frames (no row of it left): its track is
 * kept, ageing, at the distance it had from the stronger one, and the frame
 * that sees it again takes it back to that track. */
static void test_path_that_disappears_keeps_its_track(void)
{
    struct freedv *f = open_pt(FREEDV_MODE_DATAC1);
    struct OFDM *o = f->ofdm;
    int base2[2] = {-58, -2};
    double both[2] = {1.0, 0.6};
    rng_s = 13;
    ofdm_profile_reset(o, 0);
    for (int frame = 0; frame < 4; frame++)
        TEST_ASSERT_EQUAL_INT(2, step_frame(o, 0, frame == 0, 2, base2, both));
    TEST_ASSERT_EQUAL_INT(2, o->pt_ntrk);
    float gap = fabsf(o->pt_trk[0] - o->pt_trk[1]);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 56.0f, gap);
    for (int frame = 4; frame < 14; frame++)
        TEST_ASSERT_EQUAL_INT(1, step_frame(o, 0, 0, 1, base2, both));
    TEST_ASSERT_EQUAL_INT(2, o->pt_ntrk);
    int old = o->pt_trka[0] > o->pt_trka[1] ? 0 : 1;
    char msg[100];
    snprintf(msg, sizeof msg, "ages %d %d", o->pt_trka[0], o->pt_trka[1]);
    TEST_ASSERT_EQUAL_INT_MESSAGE(10, o->pt_trka[old], msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, o->pt_trka[1 - old], msg);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1.0f, gap, fabsf(o->pt_trk[0] - o->pt_trk[1]), msg);
    /* it comes back: the same two tracks, no third */
    TEST_ASSERT_EQUAL_INT(2, step_frame(o, 0, 0, 2, base2, both));
    TEST_ASSERT_EQUAL_INT(2, o->pt_ntrk);
    TEST_ASSERT_EQUAL_INT(0, o->pt_trka[0]);
    TEST_ASSERT_EQUAL_INT(0, o->pt_trka[1]);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, gap, fabsf(o->pt_trk[0] - o->pt_trk[1]));
    freedv_close(f);
}

/* Noise only: K = 4 times what a noise lag explains was chosen in the lab.
 * Over four rows and 27 carriers a frame of pure noise is taken for a path
 * about once in a hundred; the test pins that to under 3 %. */
static void test_noise_only_yields_no_path(void)
{
    struct freedv *f = open_pt(FREEDV_MODE_DATAC1);
    struct OFDM *o = f->ofdm;
    complex float H[4 * 32];
    int nfalse = 0, trials = 1000;
    rng_s = 17;
    for (int k = 0; k < trials; k++) {
        int lag[OFDM_PT_MAXPATHS];
        float pw[OFDM_PT_MAXPATHS], ratio;
        make_rows(o, 4, 0, NULL, NULL, 1.0, H);
        if (ofdm_profile_paths(o, H, 4, lag, pw, &ratio) > 0) nfalse++;
    }
    char msg[80];
    snprintf(msg, sizeof msg, "noise frames taken for a path: %d of %d", nfalse, trials);
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(nfalse < trials * 3 / 100, msg);
    freedv_close(f);
}

/* -------------------------------------------------- the live demodulator */

/* A clean DATAC1 burst through the stock receiver: the pilot rows it holds
 * (this and next, valid on every frame) show one path at the lag the lab
 * measured for the stock window, and the placement leaves it alone. */
static void test_clean_burst_one_path_no_shift(void)
{
    struct freedv *tx = freedv_open(FREEDV_MODE_DATAC1), *rx = open_pt(FREEDV_MODE_DATAC1);
    freedv_set_frames_per_burst(tx, 1);
    freedv_set_frames_per_burst(rx, 1);
    int ntx = freedv_get_n_tx_modem_samples(tx);
    short *buf = malloc(sizeof(short) * (size_t)(3200 + 8 * ntx));
    unsigned char payload[2048], out[2048];
    short chunk[8192];
    rng_s = 5;
    int total = make_trial(tx, 100.0, 0, 0.0, buf, payload);
    struct OFDM *o = rx->ofdm;
    int nc = o->nc, pos = 0, frames = 0, bad = 0, shifted = 0;
    int lo = 0, hi = -999;
    freedv_set_sync(rx, FREEDV_SYNC_UNSYNC);
    while (pos < total) {
        int nin = freedv_nin(rx);
        for (int i = 0; i < nin; i++) chunk[i] = pos + i < total ? buf[pos + i] : 0;
        pos += nin;
        State before = o->sync_state;
        freedv_rawdatarx(rx, out, chunk);
        if (before != trial && before != synced) continue;
        complex float H[2 * 32];
        for (int i = 0; i < nc; i++) {
            H[i] = o->rx_sym[1][i + 1] * conjf(o->pilots[i + 1]);
            H[nc + i] = o->rx_sym[o->ns + 1][i + 1] * conjf(o->pilots[i + 1]);
        }
        int lag[OFDM_PT_MAXPATHS];
        float pw[OFDM_PT_MAXPATHS], ratio;
        int n = ofdm_profile_paths(o, H, 2, lag, pw, &ratio);
        ofdm_profile_reset(o, o->sample_point);
        int d = ofdm_profile_place(o, n, lag, pw, 1);
        frames++;
        if (n != 1) { bad++; continue; }
        if (lag[0] < lo) lo = lag[0];
        if (lag[0] > hi) hi = lag[0];
        if (d != 0) shifted++;
    }
    char msg[120];
    snprintf(msg, sizeof msg, "%d frames, %d without exactly one path, lag %d..%d, %d shifted", frames, bad, lo, hi, shifted);
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(frames >= 38, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, bad, msg);
    TEST_ASSERT_TRUE_MESSAGE(lo >= -46 && hi <= -38, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, shifted, msg);
    free(buf);
    freedv_close(tx); freedv_close(rx);
}

/* The replay with the stock timing tracker and the stock window reproduces
 * the symbols the live demodulator produced (its four-pilot rows, which is
 * what the attempt decodes from) to within rounding.  It cannot be bit for
 * bit: the down-conversion's phase reference is the buffer index, which the
 * burst no longer has when the replay runs.  It also proves that the first
 * frame is found again in the buffer, through the running sample count. */
static void test_replay_with_stock_timing_matches_live(void)
{
    struct freedv *tx = freedv_open(FREEDV_MODE_DATAC1), *rx = open_pt(FREEDV_MODE_DATAC1);
    freedv_set_frames_per_burst(tx, 1);
    freedv_set_frames_per_burst(rx, 0);       /* keep the samples after the packet */
    freedv_set_chanest_alt(rx, OFDM_CHANEST_T4);
    struct OFDM *o = rx->ofdm;
    ofdm_profile_test_stock_timing(o, 1);
    int ntx = freedv_get_n_tx_modem_samples(tx);
    int np = o->np;
    size_t nsym = (size_t)o->rowsperframe * (size_t)o->nc;
    short *buf = malloc(sizeof(short) * (size_t)(3200 + 8 * ntx));
    complex float *ref = calloc(nsym * (size_t)np, sizeof(complex float));
    float *refa = calloc(nsym * (size_t)np, sizeof(float));
    complex float *rep = calloc(nsym * (size_t)np, sizeof(complex float));
    float *repa = calloc(nsym * (size_t)np, sizeof(float));
    unsigned char payload[2048], out[2048];
    short chunk[8192];
    rng_s = 5;
    /* a little noise, so the pilots are not exact and the rounding is real */
    int total = make_trial(tx, 20.0, 0, 0.0, buf, payload);
    int pos = 0, k = 0, replayed = 0;
    float live_mean_amp = 0.0f, rep_mean_amp = 0.0f;
    freedv_set_sync(rx, FREEDV_SYNC_UNSYNC);
    while (pos < total && !replayed) {
        int nin = freedv_nin(rx);
        for (int i = 0; i < nin; i++) chunk[i] = pos + i < total ? buf[pos + i] : 0;
        pos += nin;
        State before = o->sync_state;
        freedv_rawdatarx(rx, out, chunk);
        if (before != trial && before != synced) continue;
        if (k < np) {
            memcpy(&ref[(size_t)k * nsym], o->rx_np_alt, nsym * sizeof(complex float));
            memcpy(&refa[(size_t)k * nsym], o->rx_amp_alt, nsym * sizeof(float));
        }
        k++;
        if (k == np) {
            live_mean_amp = o->mean_amp;
            replayed = ofdm_profile_replay(o, rep, repa, &rep_mean_amp);
        }
    }
    TEST_ASSERT_EQUAL_INT(np, k);
    TEST_ASSERT_TRUE_MESSAGE(replayed, "the first frame was not found again in the buffer");
    TEST_ASSERT_EQUAL_INT(0, ofdm_profile_nerased(o));
    double e = 0.0, p = 0.0, ea = 0.0, pa = 0.0;
    for (size_t i = 0; i < nsym * (size_t)np; i++) {
        complex float d = rep[i] - ref[i];
        e += crealf(d) * crealf(d) + cimagf(d) * cimagf(d);
        p += crealf(ref[i]) * crealf(ref[i]) + cimagf(ref[i]) * cimagf(ref[i]);
        ea += (repa[i] - refa[i]) * (repa[i] - refa[i]);
        pa += refa[i] * refa[i];
    }
    char msg[160];
    snprintf(msg, sizeof msg, "replay against live over %d frames: symbols %.2e, amplitudes %.2e, mean_amp %.5f against %.5f",
             np, sqrt(e / p), sqrt(ea / pa), (double)rep_mean_amp, (double)live_mean_amp);
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(p > 0.0, msg);
    TEST_ASSERT_TRUE_MESSAGE(sqrt(e / p) < 1e-3, msg);
    TEST_ASSERT_TRUE_MESSAGE(sqrt(ea / pa) < 1e-3, msg);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-3f * live_mean_amp, live_mean_amp, rep_mean_amp, msg);
    free(buf); free(ref); free(refa); free(rep); free(repa);
    freedv_close(tx); freedv_close(rx);
}

/* The two-path channel of the last tests, also used by the untouched-live
 * check: a second path 56 samples (7 ms) behind, +12 dB; where the powers
 * swap, with a period of 1.5 s. */
#define TWO_PATH_DELAY 56
#define TWO_PATH_AMP 0.8
#define TWO_PATH_DB 12.0
#define TWO_PATH_ALT 12000

/* With the feature on, the live demodulator is exactly what it is with it
 * off: after every call the same timing, sample point, frequency, rxbufst,
 * nin, and the same bits in the stock, 09 and 19 symbol buffers and in the
 * sample buffer, across bursts where the new attempt runs (and delivers). */
static void check_live_untouched(int with_09_19)
{
    struct freedv *tx = freedv_open(FREEDV_MODE_DATAC1);
    struct freedv *a = freedv_open(FREEDV_MODE_DATAC1), *b = freedv_open(FREEDV_MODE_DATAC1);
    freedv_set_frames_per_burst(tx, 1);
    freedv_set_frames_per_burst(a, 1);
    freedv_set_frames_per_burst(b, 1);
    /* with the 09 and 19 attempts on, their symbol buffers are compared too;
     * on this channel they rescue some bursts before the new attempt is
     * reached, so the variant without them is the one where it surely runs */
    freedv_set_chanest_alt(a, with_09_19 ? OFDM_CHANEST_T4F3 : OFDM_CHANEST_OFF);
    freedv_set_chanest_alt(b, with_09_19 ? OFDM_CHANEST_T4F3 : OFDM_CHANEST_OFF);
    freedv_set_cp_recycle(a, with_09_19 ? 4 : 0);
    freedv_set_cp_recycle(b, with_09_19 ? 4 : 0);
    freedv_set_profile_track(b, 1);
    TEST_ASSERT_EQUAL_INT(0, freedv_get_profile_track(a));
    TEST_ASSERT_EQUAL_INT(1, freedv_get_profile_track(b));
    struct OFDM *oa = a->ofdm, *ob = b->ofdm;
    int ntx = freedv_get_n_tx_modem_samples(tx);
    int nbytes = freedv_get_bits_per_modem_frame(tx) / 8;
    short *buf = malloc(sizeof(short) * (size_t)(3200 + 8 * ntx));
    unsigned char payload[2048], oa_out[2048], ob_out[2048];
    short chunk[8192];
    size_t nsym = (size_t)oa->rowsperframe * (size_t)oa->nc;
    int calls = 0, delivered_b = 0, delivered_a = 0, by_pt = 0;
    rng_s = 99;
    alt_period = TWO_PATH_ALT;   /* stock fails here, so the new attempt runs */
    for (int trial_i = 0; trial_i < 4; trial_i++) {
        int total = make_trial(tx, TWO_PATH_DB, TWO_PATH_DELAY, TWO_PATH_AMP, buf, payload);
        int pos = 0;
        freedv_set_sync(a, FREEDV_SYNC_UNSYNC);
        freedv_set_sync(b, FREEDV_SYNC_UNSYNC);
        while (pos < total) {
            int nin = freedv_nin(a);
            TEST_ASSERT_EQUAL_INT(nin, freedv_nin(b));
            if (nin > 8192) break;
            for (int i = 0; i < nin; i++) chunk[i] = pos + i < total ? buf[pos + i] : 0;
            pos += nin;
            int na = (int)freedv_rawdatarx(a, oa_out, chunk);
            int nb = (int)freedv_rawdatarx(b, ob_out, chunk);
            calls++;
            if (na > 0 && !(freedv_get_rx_status(a) & FREEDV_RX_BIT_ERRORS) && !memcmp(oa_out, payload, (size_t)nbytes - 2)) delivered_a++;
            if (nb > 0 && !(freedv_get_rx_status(b) & FREEDV_RX_BIT_ERRORS) && !memcmp(ob_out, payload, (size_t)nbytes - 2)) {
                delivered_b++;
                if (freedv_get_rx_decode_path(b) == 4) by_pt++;
            }
            TEST_ASSERT_EQUAL_INT(oa->timing_est, ob->timing_est);
            TEST_ASSERT_EQUAL_INT(oa->sample_point, ob->sample_point);
            TEST_ASSERT_EQUAL_INT(oa->rxbufst, ob->rxbufst);
            TEST_ASSERT_EQUAL_INT(oa->nin, ob->nin);
            TEST_ASSERT_EQUAL_INT(oa->sync_state, ob->sync_state);
            TEST_ASSERT_EQUAL_INT(oa->modem_frame, ob->modem_frame);
            TEST_ASSERT_EQUAL_INT(0, memcmp(&oa->foff_est_hz, &ob->foff_est_hz, sizeof(float)));
            TEST_ASSERT_EQUAL_INT(0, memcmp(&oa->mean_amp, &ob->mean_amp, sizeof(float)));
            TEST_ASSERT_EQUAL_INT(0, memcmp(oa->rx_np, ob->rx_np, nsym * sizeof(complex float)));
            TEST_ASSERT_EQUAL_INT(0, memcmp(oa->rx_amp, ob->rx_amp, nsym * sizeof(float)));
            TEST_ASSERT_EQUAL_INT(0, memcmp(oa->rx_np_alt, ob->rx_np_alt, nsym * sizeof(complex float)));
            TEST_ASSERT_EQUAL_INT(0, memcmp(oa->rx_amp_alt, ob->rx_amp_alt, nsym * sizeof(float)));
            TEST_ASSERT_EQUAL_INT(0, memcmp(oa->rx_np_cp, ob->rx_np_cp, nsym * sizeof(complex float)));
            TEST_ASSERT_EQUAL_INT(0, memcmp(oa->rx_amp_cp, ob->rx_amp_cp, nsym * sizeof(float)));
            TEST_ASSERT_EQUAL_INT(0, memcmp(oa->rxbuf, ob->rxbuf, (size_t)oa->nrxbuf * sizeof(complex float)));
            for (int r = 0; r < oa->ns + 3; r++) {
                TEST_ASSERT_EQUAL_INT(0, memcmp(oa->rx_sym[r], ob->rx_sym[r], (size_t)(oa->nc + 2) * sizeof(complex float)));
                TEST_ASSERT_EQUAL_INT(0, memcmp(oa->rx_sym_cp[r], ob->rx_sym_cp[r], (size_t)(oa->nc + 2) * sizeof(complex float)));
            }
            TEST_ASSERT_EQUAL_INT(0, memcmp(a->rx_syms_alt, b->rx_syms_alt, (size_t)oa->bitsperpacket / 2 * sizeof(COMP)));
            TEST_ASSERT_EQUAL_INT(0, memcmp(a->rx_syms_cp, b->rx_syms_cp, (size_t)oa->bitsperpacket / 2 * sizeof(COMP)));
            TEST_ASSERT_EQUAL_INT(0, memcmp(a->rx_syms, b->rx_syms, (size_t)oa->bitsperpacket / 2 * sizeof(COMP)));
        }
    }
    alt_period = 0;
    char msg[120];
    snprintf(msg, sizeof msg, "%d calls compared; delivered off %d, on %d (%d by the new attempt)", calls, delivered_a, delivered_b, by_pt);
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(delivered_b >= delivered_a, msg);
    /* the replay really ran in this comparison, or it would prove nothing */
    if (!with_09_19) TEST_ASSERT_TRUE_MESSAGE(by_pt >= 1, msg);
    free(buf);
    freedv_close(tx); freedv_close(a); freedv_close(b);
}

static void test_live_demodulator_untouched(void) { check_live_untouched(0); }
static void test_live_demodulator_untouched_with_09_and_19(void) { check_live_untouched(1); }

/* ------------------------------------------------------------ the attempt */

typedef struct { int base, pt, added, lost, by_pt; } tally_t;

/* The same samples to a stock receiver and one with the feature on. */
static tally_t sweep(int mode, double snr3k, int echo_delay, double echo_amp,
                     int trials, uint64_t seed)
{
    struct freedv *tx = freedv_open(mode), *rx0 = freedv_open(mode), *rx1 = freedv_open(mode);
    TEST_ASSERT_NOT_NULL(tx); TEST_ASSERT_NOT_NULL(rx0); TEST_ASSERT_NOT_NULL(rx1);
    freedv_set_frames_per_burst(tx, 1);
    freedv_set_frames_per_burst(rx0, 1);
    freedv_set_frames_per_burst(rx1, 1);
    freedv_set_profile_track(rx1, 1);
    TEST_ASSERT_EQUAL_INT(1, freedv_get_profile_track(rx1));
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
        t.base += a; t.pt += b;
        if (b && !a) t.added++;
        if (a && !b) t.lost++;
        if (b && path == 4) t.by_pt++;
    }
    free(buf);
    freedv_close(tx); freedv_close(rx0); freedv_close(rx1);
    return t;
}

/* Well above the cliff every frame decodes on the stock attempt: the new
 * attempt is not reached. */
static void test_strong_signal_uses_stock_path(void)
{
    tally_t t = sweep(FREEDV_MODE_DATAC1, 20.0, 0, 0.0, 6, 11);
    TEST_ASSERT_EQUAL_INT(6, t.base);
    TEST_ASSERT_EQUAL_INT(6, t.pt);
    TEST_ASSERT_EQUAL_INT(0, t.by_pt);
}

/* Two paths 56 samples apart against a 48-sample prefix, 10 seeded bursts:
 * the stock window leaves a whole symbol's worth of the other path inside it,
 * the tracker places the window where the stronger path is clean.  Frames are
 * added, each through decode path 4 and each the frame that was sent, and
 * none is lost.  Measured (this seed, +12 dB):
 *   powers swapping with a 1.5 s period, late path 0.8:  stock 0, with 10
 *   static, both paths 1.0:                              stock 6, with 10
 *   static, early 1.0, late 0.8:                         stock 10, with 10
 * so the last one cannot separate the two and only "loses none" is asserted
 * on it; a static channel has no fade to lose the stock tracker. */
static void check_two_path(int period, double amp, int min_added, const char *what)
{
    alt_period = period;
    tally_t t = sweep(FREEDV_MODE_DATAC1, TWO_PATH_DB, TWO_PATH_DELAY, amp, 10, 2026);
    alt_period = 0;
    char msg[200];
    snprintf(msg, sizeof msg, "%s, %d samples apart, %.0f dB: stock %d of 10, with the attempt %d, added %d, lost %d, by the attempt %d",
             what, TWO_PATH_DELAY, TWO_PATH_DB, t.base, t.pt, t.added, t.lost, t.by_pt);
    TEST_MESSAGE(msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, t.lost, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(t.pt - t.base, t.added, msg);
    TEST_ASSERT_TRUE_MESSAGE(t.by_pt >= t.added, msg);
    TEST_ASSERT_TRUE_MESSAGE(t.added >= min_added, msg);
}

static void test_two_path_swapping_powers_adds_frames(void)
{
    check_two_path(TWO_PATH_ALT, TWO_PATH_AMP, 8, "powers swapping every 1.5 s");
}

static void test_two_path_static_equal_adds_frames(void)
{
    check_two_path(0, 1.0, 2, "static, equal powers");
}

static void test_two_path_static_unequal_loses_none(void)
{
    check_two_path(0, TWO_PATH_AMP, 0, "static, late path 0.8");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_off_by_default_and_few_carriers_refuse);
    RUN_TEST(test_detect_one_path);
    RUN_TEST(test_detect_two_paths_early_stronger);
    RUN_TEST(test_detect_two_paths_late_stronger);
    RUN_TEST(test_path_that_disappears_keeps_its_track);
    RUN_TEST(test_noise_only_yields_no_path);
    RUN_TEST(test_clean_burst_one_path_no_shift);
    RUN_TEST(test_replay_with_stock_timing_matches_live);
    RUN_TEST(test_live_demodulator_untouched);
    RUN_TEST(test_live_demodulator_untouched_with_09_and_19);
    RUN_TEST(test_strong_signal_uses_stock_path);
    RUN_TEST(test_two_path_swapping_powers_adds_frames);
    RUN_TEST(test_two_path_static_equal_adds_frames);
    RUN_TEST(test_two_path_static_unequal_loses_none);
    return UNITY_END();
}
