/* rx_bench -- paired sweep of receiver variants.
 *
 * One transmitted burst per trial and ONE impaired audio buffer, decoded by
 * every variant, so a difference between columns is the variant and nothing
 * else.  (acquire_vs_decode measures one receiver; comparing two of its runs
 * compares two noise realisations as well.)
 *
 *   rx_bench MODE trials lo hi step chan [variants] [tone_hz tone_db] [imp_rate imp_db] [fsk_hz fsk_db]
 *
 *   chan      awgn (lo/hi are SNR3k in dB), a watterson preset name (lo/hi
 *             are the noise density No; the achieved SNR3k is printed), or
 *             noise: no burst, only the noise a burst at that SNR3k would
 *             have had (the false-accept control: every frame "delivered" is
 *             one that was never sent)
 *   variants  comma list, default "stock".  Each variant is a set of EXACT
 *             tokens joined with '+' (a substring match once made "it400"
 *             also switch on "t4": handbook pitfall 11); an unknown token is
 *             an error.  Tokens:
 *               stock  nothing extra (the plain receiver)
 *               t4 t4f3 t4f5  the alternative channel estimate as the second
 *                      decode attempt (freedv_set_chanest_alt, proposal 09)
 *               cpN    cyclic-prefix recycling with a guard of N samples as
 *                      one more attempt after the others (proposal 19); with
 *                      no t4* token it uses t4f3, or t4 on modes of fewer
 *                      than 9 carriers
 *               pt     window placement from the pilots' delay profile as one
 *                      more attempt after the others, the burst demodulated
 *                      again with the tracker in charge of the FFT window and
 *                      the frame grid (proposal 26); decodes from the t4 rows
 *                      whatever estimator token is given; modes of fewer than
 *                      9 carriers refuse it
 *               nw     noise-aware soft bits (proposal 07): when the pilots'
 *                      residuals single out carriers, one more attempt with
 *                      those carriers scaled down, from the best rows the
 *                      other tokens give (recycled, else alternative
 *                      estimate, else stock)
 *               bK     the impulse blanker at threshold K (proposal 08,
 *                      modem/rx_blanker.c) run over the audio before the
 *                      decoder, as mercury's receive thread does; b6 is the
 *                      recommended setting
 *               nt     the automatic carrier notch (modem/rx_notch.c) run
 *                      over the audio before the decoder (and before the
 *                      blanker, as in mercury).  A carrier on the air has
 *                      been there for a while when a burst arrives, so the
 *                      notch first sees PREROLL_S seconds of the trial's
 *                      noise and tone, which the decoder does not get
 *               bp     a band-pass in front of the decoder, centred on the
 *                      mode's centre and 100 Hz wider than its occupied band
 *                      (401-tap linear-phase FIR, delay compensated).  A
 *                      BENCH-ONLY probe for proposal 16's phase 0: the modes
 *                      without a receive filter (DATAC3, DATAC1) lose
 *                      acquisition to an interferer well outside their band,
 *                      and this says whether a plain filter, not a move,
 *                      would already cure it.  Not in mercury.
 *               es     early termination of LDPC decodes that are going
 *                      nowhere (proposal 23), on every attempt of the variant
 *               dd     decision-directed channel estimation as the last
 *                      attempt (proposal 20): the best failed attempt's soft
 *                      bits become channel references for one more estimate
 *                      and decode, up to two rounds
 *             e.g. stock,t4f5,t4f5+cp4,t4f5+cp20,pt,t4f3+cp4+pt
 *   tone      an unmodulated carrier at tone_hz, tone_db relative to the
 *             burst's total power
 *   imp       imp_rate noise bursts per second, 2 ms each, imp_db above the
 *             burst's rms
 *   fsk       a MODULATED narrowband interferer (proposal 16, phase 0): 2-FSK
 *             centred on fsk_hz, fsk_db relative to the burst's total power
 *             (as the tone), continuous phase, random data.  45.45 baud and
 *             170 Hz shift (RTTY) unless RX_BENCH_FSK_BAUD / RX_BENCH_FSK_SHIFT
 *             say otherwise.  The notch cannot remove it: it is what a
 *             sub-band move would have to dodge.  Its data and phase come
 *             from a generator of their own, so trials that do not ask for it
 *             are the same samples as before.  Pass "0 -99" for the tone and
 *             "0 0" for the impulses to leave them out and still reach this
 *             one.  Like the tone it is already on the air before the burst:
 *             the notch's pre-roll sees the same signal continued backwards.
 *
 * Environment: RX_BENCH_CENTRE=Hz opens the mode, transmitter and every
 * receiver, with its centre at that audio frequency instead of 1500 Hz (the
 * DATA_CUSTOM open of freedv_open_advanced with the mode's own configuration
 * and the two centres changed; bench only, nothing in modem/freedv changes).
 * The carriers sit on the symbol-rate grid, so the centre lands on the
 * nearest one.  Tone, FSK and impulses stay where they are asked to be.
 * RX_BENCH_TIME=1 adds the CPU seconds per trial of each
 * variant; RX_BENCH_WRONG_SEED=1 is the wrong-session control (the receivers
 * expect a seeded CRC the transmitter does not use: delivered must be 0).
 *
 * Cells are acquired/delivered: acquired = full RX_SYNC, delivered = CRC ok
 * AND the bytes that were sent.  "any" counts trials at least one variant
 * delivered.  The first stage of docs/improvements/00-bench.md.
 *
 * Copyright (C) 2026 Rhizomatica
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "bench_common.h"
#include "chanutil.h"
#include "freedv_api.h"
#include "codec2_ofdm.h"
#include "ofdm_internal.h"
#include "rx_blanker.h"
#include "rx_notch.h"

#define FS 8000.0
#define HEADROOM_PEAK 4000.0
#define PREROLL_S 3


static bench_rng rng = {1357911131517ULL};
static double gauss(void) { return bench_rng_gauss(&rng); }
static double zero_noise(void *ctx) { (void)ctx; return 0.0; }

typedef struct { int chanest; int cp; int pt; int nw; int notch; int bp; int es; int dd; float blank; char name[64]; } variant_t;

/* Parse one variant: '+'-joined exact tokens.  0 on success. */
static int parse_variant(const char *s, variant_t *v) {
  char tmp[64];
  int have_est = 0, have_cp = 0, have_stock = 0, have_pt = 0;
  if (strlen(s) >= sizeof(tmp) || !*s) return -1;
  strcpy(tmp, s);
  strcpy(v->name, s);
  v->chanest = OFDM_CHANEST_OFF;
  v->cp = 0;
  v->pt = 0;
  v->nw = 0;
  v->blank = 0.0f;
  v->notch = 0;
  v->bp = 0;
  v->es = 0;
  v->dd = 0;
  char *save = NULL;
  for (char *t = strtok_r(tmp, "+", &save); t; t = strtok_r(NULL, "+", &save)) {
    int est = -1;
    if (!strcmp(t, "stock")) { have_stock = 1; continue; }
    else if (!strcmp(t, "pt")) {
      if (have_pt) { fprintf(stderr, "variant '%s': two pt tokens\n", s); return -1; }
      have_pt = 1;
      v->pt = 1;
      continue;
    }
    else if (!strcmp(t, "nw")) { v->nw = 1; continue; }
    else if (!strcmp(t, "nt")) { v->notch = 1; continue; }
    else if (!strcmp(t, "bp")) { v->bp = 1; continue; }
    else if (!strcmp(t, "es")) { v->es = 1; continue; }
    else if (!strcmp(t, "dd")) { v->dd = 1; continue; }
    else if (!strcmp(t, "t4")) est = OFDM_CHANEST_T4;
    else if (!strcmp(t, "t4f3")) est = OFDM_CHANEST_T4F3;
    else if (!strcmp(t, "t4f5")) est = OFDM_CHANEST_T4F5;
    if (est >= 0) {
      if (have_est) { fprintf(stderr, "variant '%s': two estimator tokens\n", s); return -1; }
      have_est = 1;
      v->chanest = est;
      continue;
    }
    if (t[0] == 'b' && t[1] >= '0' && t[1] <= '9') {
      char *end;
      double kb = strtod(t + 1, &end);
      if (*end || kb < 1.0 || kb > 100.0) { fprintf(stderr, "variant '%s': bad threshold in '%s'\n", s, t); return -1; }
      v->blank = (float)kb;
      continue;
    }
    if (t[0] == 'c' && t[1] == 'p' && t[2] >= '0' && t[2] <= '9') {
      char *end;
      long n = strtol(t + 2, &end, 10);
      if (*end || n < 1 || n > 1000) { fprintf(stderr, "variant '%s': bad guard in '%s'\n", s, t); return -1; }
      if (have_cp) { fprintf(stderr, "variant '%s': two cp tokens\n", s); return -1; }
      have_cp = 1;
      v->cp = (int)n;
      continue;
    }
    fprintf(stderr, "unknown variant token '%s' in '%s': stock, t4, t4f3, t4f5, cpN, pt, nw, bK, nt, bp, es, dd\n", t, s);
    return -1;
  }
  if (have_stock && (have_est || have_cp || have_pt || v->nw || v->blank > 0.0f || v->notch || v->bp || v->es || v->dd)) {
    fprintf(stderr, "variant '%s': 'stock' cannot be combined\n", s);
    return -1;
  }
  return 0;
}

/* Occupied width of the narrow modes (docs/MODES.md), for the bp probe. */
static double mode_width_hz(const char *name) {
  if (!strcasecmp(name, "DATAC15") || !strcasecmp(name, "DATAC16")) return 187.0;
  if (!strcasecmp(name, "DATAC4")) return 250.0;
  if (!strcasecmp(name, "DATAC3")) return 562.0;
  return 1000.0;
}

/* Linear-phase windowed-sinc band-pass of `bw` Hz around `fc`, applied in
 * place with its delay taken out so the burst stays where it was. */
#define BP_TAPS 401
static void bandpass(short *x, int n, double fc, double bw) {
  static double h[BP_TAPS];
  static double fcs = -1.0, bws = -1.0;
  if (fc != fcs || bw != bws) {
    double f1 = (fc - bw / 2.0) / FS, f2 = (fc + bw / 2.0) / FS;
    int m = BP_TAPS / 2;
    for (int i = 0; i < BP_TAPS; i++) {
      int d = i - m;
      double w = 0.54 - 0.46 * cos(2.0 * M_PI * i / (BP_TAPS - 1));
      h[i] = w * (d == 0 ? 2.0 * (f2 - f1) : (sin(2.0 * M_PI * f2 * d) - sin(2.0 * M_PI * f1 * d)) / (M_PI * d));
    }
    fcs = fc;
    bws = bw;
  }
  short *y = malloc(sizeof(short) * (size_t)n);
  int m = BP_TAPS / 2;
  for (int i = 0; i < n; i++) {
    double a = 0.0;
    for (int j = 0; j < BP_TAPS; j++) {
      int k = i + m - j;
      if (k >= 0 && k < n) a += h[j] * x[k];
    }
    y[i] = (short)lrint(a > 32767.0 ? 32767.0 : (a < -32768.0 ? -32768.0 : a));
  }
  memcpy(x, y, sizeof(short) * (size_t)n);
  free(y);
}

/* Open `mode`, at `centre` Hz when that is not 0 (RX_BENCH_CENTRE).  The
 * custom open takes the mode's own configuration from ofdm_init_mode and
 * changes only the two centres; the mode name stays, so the filters and the
 * state machine are the mode's.  With centre 0 it is the plain open. */
static struct freedv *open_mode(int mode, const char *name, double centre) {
  if (centre <= 0.0) return freedv_open(mode);
  struct OFDM_CONFIG cfg;
  char lname[16];
  size_t i;
  for (i = 0; name[i] && i < sizeof(lname) - 1; i++) lname[i] = (char)tolower((unsigned char)name[i]);
  lname[i] = 0;
  ofdm_init_mode(lname, &cfg);
  cfg.tx_centre = cfg.rx_centre = (float)centre;
  struct freedv_advanced adv = {0, 2, 100, 8000, 1000, 200, "H_256_512_4", &cfg};
  return freedv_open_advanced(FREEDV_MODE_DATA_CUSTOM, &adv);
}

int main(int argc, char **argv) {
  if (argc < 7) {
    fprintf(stderr, "usage: rx_bench MODE trials lo hi step chan [variants] [tone_hz tone_db] [imp_rate imp_db] [fsk_hz fsk_db]\n");
    return 1;
  }
  int mode = bench_mode_from_name(argv[1]);
  int trials = atoi(argv[2]);
  double lo = atof(argv[3]), hi = atof(argv[4]), step = atof(argv[5]);
  /* "noise": no burst at all, only the noise (and the tone and impulses, if
   * asked for) that a burst at the row's SNR would have had around it.  The
   * false-accept control: "delivered" then counts ANY frame a variant
   * returned, and every one of them is a frame that was never sent. */
  int noise_only = !strcmp(argv[6], "noise");
  int chan = noise_only ? CHAN_AWGN : chanutil_preset_from_name(argv[6]);
  /* RX_BENCH_WRONG_SEED=1: the receivers expect a session's seeded CRC and
   * accept no plain one, and the transmitter sends plain frames: every frame
   * is "another session's".  The wrong-session control: delivered must be 0
   * however good the signal, whatever attempts a variant adds. */
  int wrong_seed = getenv("RX_BENCH_WRONG_SEED") != NULL;
  const char *vlist = argc > 7 ? argv[7] : "stock";
  double tone_hz = argc > 9 ? atof(argv[8]) : 0.0;
  double tone_db = argc > 9 ? atof(argv[9]) : -99.0;
  double imp_rate = argc > 11 ? atof(argv[10]) : 0.0;
  double imp_db = argc > 11 ? atof(argv[11]) : 0.0;
  double fsk_hz = argc > 13 ? atof(argv[12]) : 0.0;
  double fsk_db = argc > 13 ? atof(argv[13]) : -99.0;
  double fsk_baud = getenv("RX_BENCH_FSK_BAUD") ? atof(getenv("RX_BENCH_FSK_BAUD")) : 45.45;
  double fsk_shift = getenv("RX_BENCH_FSK_SHIFT") ? atof(getenv("RX_BENCH_FSK_SHIFT")) : 170.0;
  double centre = getenv("RX_BENCH_CENTRE") ? atof(getenv("RX_BENCH_CENTRE")) : 0.0;
  if (mode < 0 || chan < 0) { fprintf(stderr, "bad mode/channel\n"); return 1; }

  variant_t v[16];
  int nv = 0;
  char tmp[256];
  strncpy(tmp, vlist, sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = 0;
  for (char *t = strtok(tmp, ","); t && nv < 16; t = strtok(NULL, ",")) {
    if (parse_variant(t, &v[nv])) return 1;
    nv++;
  }

  struct freedv *tx = open_mode(mode, argv[1], centre);
  if (!tx) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
  struct freedv *rx[16];
  for (int k = 0; k < nv; k++) {
    rx[k] = open_mode(mode, argv[1], centre);
    freedv_set_frames_per_burst(rx[k], 1);
    if (wrong_seed) freedv_set_crc_seed(rx[k], 0x5a17, 0);
    freedv_set_chanest_alt(rx[k], v[k].chanest);
    freedv_set_cp_recycle(rx[k], v[k].cp);
    freedv_set_profile_track(rx[k], v[k].pt);
    freedv_set_noise_weight(rx[k], v[k].nw);
    freedv_set_ldpc_early_stop(rx[k], v[k].es);
    freedv_set_decision_directed(rx[k], v[k].dd);
    if (freedv_get_profile_track(rx[k]) != v[k].pt) {
      fprintf(stderr, "variant '%s': %s has too few carriers for profile tracking\n", v[k].name, argv[1]);
      return 1;
    }
    if (freedv_get_cp_recycle(rx[k]) != v[k].cp) {
      fprintf(stderr, "variant '%s': guard %d is longer than the %s prefix\n", v[k].name, v[k].cp, argv[1]);
      return 1;
    }
  }
  freedv_set_frames_per_burst(tx, 1);

  int nbytes = freedv_get_bits_per_modem_frame(tx) / 8;
  int nburst = freedv_get_n_tx_preamble_modem_samples(tx) + freedv_get_n_tx_modem_samples(tx) +
               freedv_get_n_tx_postamble_modem_samples(tx);
  int lead = (int)(0.4 * FS), tail = nburst;
  int ntot = lead + nburst + tail;

  short *burst = malloc(sizeof(short) * (size_t)(nburst + 4));
  short *buf = malloc(sizeof(short) * (size_t)ntot);
  short *wbuf = malloc(sizeof(short) * (size_t)ntot);
  const int npre = PREROLL_S * (int)FS;
  double *fsk = malloc(sizeof(double) * (size_t)(npre + ntot));
  unsigned char *payload = malloc((size_t)nbytes), *out = malloc((size_t)nbytes + 8);

  printf("%s %d B, burst %.2f s, chan %s, tone %.0f Hz %+.0f dB, impulses %.1f/s %+.0f dB", argv[1], nbytes,
         nburst / FS, noise_only ? "NOISE ONLY (no burst)" : chanutil_preset_name(chan), tone_hz, tone_db, imp_rate,
         imp_db);
  if (fsk_db > -90.0) printf(", FSK %.0f Hz %+.0f dB (%.2f baud, %.0f Hz shift)", fsk_hz, fsk_db, fsk_baud, fsk_shift);
  if (centre > 0.0) printf(", centre %.0f Hz", centre);
  printf("\n");
  if (wrong_seed) printf("wrong-session control: the receivers expect a seeded CRC, the bursts carry a plain one\n");
  printf("%-8s %-8s", chan == CHAN_AWGN ? "SNR3k" : "No", "meas");
  for (int k = 0; k < nv; k++) printf(" %12s", v[k].name);
  printf("   (acquired/delivered per %d)\n", trials);

  for (double x = lo; x <= hi + 1e-6; x += step) {
    int acq[16] = {0}, del[16] = {0}, uni = 0;
    double cpu[16] = {0};
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
      double snr = x, sigma;
      if (ch) {
        float m = 0.0f;
        chanutil_run(ch, (int16_t *)burst, n, &m);
        snr = m;
      }
      meas_sum += snr;
      sigma = sqrt(ps / (pow(10.0, snr / 10.0) * (3000.0 / (FS / 2.0))));
      double ta = tone_db > -90.0 ? sqrt(2.0 * ps * pow(10.0, tone_db / 10.0)) : 0.0;
      double tph = 2.0 * M_PI * bench_rng_uniform(&rng);
      double fa = fsk_db > -90.0 ? sqrt(2.0 * ps * pow(10.0, fsk_db / 10.0)) : 0.0;
      double ia = sqrt(ps) * pow(10.0, imp_db / 20.0);
      int imp_left = 0;
      /* The interferer, PREROLL_S seconds before the buffer and through it,
       * from a generator of its own so no other trial's samples move. */
      if (fa > 0.0) {
        bench_rng frng = {4242ULL + (uint64_t)t};
        double ph = 2.0 * M_PI * bench_rng_uniform(&frng), f = fsk_hz;
        long sym = -1;
        for (int i = 0; i < npre + ntot; i++) {
          long ns = (long)floor((double)i * fsk_baud / FS);
          if (ns != sym) { sym = ns; f = fsk_hz + (bench_rng_uniform(&frng) < 0.5 ? -0.5 : 0.5) * fsk_shift; }
          fsk[i] = fa * sin(ph);
          ph += 2.0 * M_PI * f / FS;
          if (ph > 2.0 * M_PI) ph -= 2.0 * M_PI;
        }
      }
      for (int i = 0; i < ntot; i++) {
        double s = 0.0;
        int bi = i - lead;
        if (bi >= 0 && bi < n && !noise_only) s = burst[bi];
        /* the fading channel already added its noise inside the burst */
        if (!ch || bi < 0 || bi >= n) s += sigma * gauss();
        else (void)gauss();
        if (ta > 0.0) s += ta * cos(2.0 * M_PI * tone_hz * i / FS + tph);
        if (fa > 0.0) s += fsk[npre + i];
        if (imp_rate > 0.0) {
          if (!imp_left && bench_rng_uniform(&rng) < imp_rate / FS) imp_left = (int)(0.002 * FS);
          double g2 = gauss();
          if (imp_left) { s += ia * g2; imp_left--; }
        }
        if (s > 32767.0) s = 32767.0;
        if (s < -32768.0) s = -32768.0;
        buf[i] = (short)lrint(s);
      }
      int any = 0;
      for (int k = 0; k < nv; k++) {
        int ss = 0, got = 0;
        const short *src = buf;
        if (v[k].blank > 0.0f || v[k].notch || v[k].bp) {
          memcpy(wbuf, buf, sizeof(short) * (size_t)ntot);
          src = wbuf;
        }
        if (v[k].notch) {
          /* the trial's own noise level and tone, continued backwards in
           * time; its own generator, so the trials stay the same samples
           * whether or not a variant asks for the notch */
          static rx_notch_t nt;
          static short pre[PREROLL_S * (int)FS];
          bench_rng prng = {977ULL + (uint64_t)t};
          rx_notch_init(&nt, true, (int)FS);
          for (int i = 0; i < PREROLL_S * (int)FS; i++) {
            int ti = i - PREROLL_S * (int)FS;
            double s = sigma * bench_rng_gauss(&prng);
            if (ta > 0.0) s += ta * cos(2.0 * M_PI * tone_hz * ti / FS + tph);
            if (fa > 0.0) s += fsk[i];
            if (s > 32767.0) s = 32767.0;
            if (s < -32768.0) s = -32768.0;
            pre[i] = (short)lrint(s);
          }
          rx_notch_process(&nt, pre, PREROLL_S * (int)FS);
          rx_notch_process(&nt, wbuf, ntot);
        }
        if (v[k].bp) bandpass(wbuf, ntot, centre > 0.0 ? centre : 1500.0, mode_width_hz(argv[1]) + 100.0);
        if (v[k].blank > 0.0f) {
          /* a fresh blanker per trial: the lead of noise before the burst
           * sets its level, as the noise before a burst does on the air */
          rx_blanker_t bl;
          rx_blanker_init(&bl, v[k].blank, (int)FS);
          rx_blanker_process(&bl, wbuf, ntot);
        }
        clock_t c0 = clock();
        bench_run_burst(rx[k], src, ntot, 0, 0.0, out, zero_noise, NULL, &ss, &got, NULL, NULL);
        cpu[k] += (double)(clock() - c0) / CLOCKS_PER_SEC;
        /* a frame that is not the one sent does not count as delivered,
         * except in the two controls, where any frame at all is the fault
         * being counted */
        if (got && !noise_only && !wrong_seed && memcmp(out, payload, (size_t)nbytes - 2)) got = 0;
        acq[k] += ss; del[k] += got; any |= got;
      }
      uni += any;
    }
    if (ch) chanutil_close(ch);
    printf("%+7.1f  %+7.2f ", x, meas_sum / trials);
    for (int k = 0; k < nv; k++) printf("    %3d/%-3d  ", acq[k], del[k]);
    printf("   any %3d", uni);
    if (getenv("RX_BENCH_TIME")) {
      /* CPU seconds per trial spent in each variant's receiver */
      printf("   cpu/trial");
      for (int k = 0; k < nv; k++) printf(" %.3f", cpu[k] / trials);
    }
    printf("\n");
    fflush(stdout);
  }
  for (int k = 0; k < nv; k++) freedv_close(rx[k]);
  freedv_close(tx);
  free(fsk); free(burst); free(buf); free(wbuf); free(payload); free(out);
  return 0;
}
