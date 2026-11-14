/* rx_bench -- paired sweep of receiver variants.
 *
 * One transmitted burst per trial and ONE impaired audio buffer, decoded by
 * every variant, so a difference between columns is the variant and nothing
 * else.  (acquire_vs_decode measures one receiver; comparing two of its runs
 * compares two noise realisations as well.)
 *
 *   rx_bench MODE trials lo hi step chan [variants] [tone_hz tone_db] [imp_rate imp_db]
 *
 *   chan      awgn (lo/hi are SNR3k in dB) or a watterson preset name (lo/hi
 *             are the noise density No; the achieved SNR3k is printed)
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
 *             e.g. stock,t4f5,t4f5+cp4,t4f5+cp20,pt,t4f3+cp4+pt
 *   tone      an unmodulated carrier at tone_hz, tone_db relative to the
 *             burst's total power
 *   imp       imp_rate noise bursts per second, 2 ms each, imp_db above the
 *             burst's rms
 *
 * Cells are acquired/delivered: acquired = full RX_SYNC, delivered = CRC ok
 * AND the bytes that were sent.  "any" counts trials at least one variant
 * delivered.  The first stage of docs/improvements/00-bench.md.
 *
 * Copyright (C) 2026 Rhizomatica
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_common.h"
#include "chanutil.h"
#include "freedv_api.h"
#include "codec2_ofdm.h"

#define FS 8000.0
#define HEADROOM_PEAK 4000.0


static bench_rng rng = {1357911131517ULL};
static double gauss(void) { return bench_rng_gauss(&rng); }
static double zero_noise(void *ctx) { (void)ctx; return 0.0; }

typedef struct { int chanest; int cp; int pt; int nw; char name[64]; } variant_t;

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
    else if (!strcmp(t, "t4")) est = OFDM_CHANEST_T4;
    else if (!strcmp(t, "t4f3")) est = OFDM_CHANEST_T4F3;
    else if (!strcmp(t, "t4f5")) est = OFDM_CHANEST_T4F5;
    if (est >= 0) {
      if (have_est) { fprintf(stderr, "variant '%s': two estimator tokens\n", s); return -1; }
      have_est = 1;
      v->chanest = est;
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
    fprintf(stderr, "unknown variant token '%s' in '%s': stock, t4, t4f3, t4f5, cpN, pt, nw\n", t, s);
    return -1;
  }
  if (have_stock && (have_est || have_cp || have_pt || v->nw)) {
    fprintf(stderr, "variant '%s': 'stock' cannot be combined\n", s);
    return -1;
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 7) {
    fprintf(stderr, "usage: rx_bench MODE trials lo hi step chan [variants] [tone_hz tone_db] [imp_rate imp_db]\n");
    return 1;
  }
  int mode = bench_mode_from_name(argv[1]);
  int trials = atoi(argv[2]);
  double lo = atof(argv[3]), hi = atof(argv[4]), step = atof(argv[5]);
  int chan = chanutil_preset_from_name(argv[6]);
  const char *vlist = argc > 7 ? argv[7] : "stock";
  double tone_hz = argc > 9 ? atof(argv[8]) : 0.0;
  double tone_db = argc > 9 ? atof(argv[9]) : -99.0;
  double imp_rate = argc > 11 ? atof(argv[10]) : 0.0;
  double imp_db = argc > 11 ? atof(argv[11]) : 0.0;
  if (mode < 0 || chan < 0) { fprintf(stderr, "bad mode/channel\n"); return 1; }

  variant_t v[16];
  int nv = 0;
  char tmp[256];
  strncpy(tmp, vlist, sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = 0;
  for (char *t = strtok(tmp, ","); t && nv < 16; t = strtok(NULL, ",")) {
    if (parse_variant(t, &v[nv])) return 1;
    nv++;
  }

  struct freedv *tx = freedv_open(mode);
  struct freedv *rx[16];
  for (int k = 0; k < nv; k++) {
    rx[k] = freedv_open(mode);
    freedv_set_frames_per_burst(rx[k], 1);
    freedv_set_chanest_alt(rx[k], v[k].chanest);
    freedv_set_cp_recycle(rx[k], v[k].cp);
    freedv_set_profile_track(rx[k], v[k].pt);
    freedv_set_noise_weight(rx[k], v[k].nw);
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
  unsigned char *payload = malloc((size_t)nbytes), *out = malloc((size_t)nbytes + 8);

  printf("%s %d B, burst %.2f s, chan %s, tone %.0f Hz %+.0f dB, impulses %.1f/s %+.0f dB\n", argv[1], nbytes,
         nburst / FS, chanutil_preset_name(chan), tone_hz, tone_db, imp_rate, imp_db);
  printf("%-8s %-8s", chan == CHAN_AWGN ? "SNR3k" : "No", "meas");
  for (int k = 0; k < nv; k++) printf(" %12s", v[k].name);
  printf("   (acquired/delivered per %d)\n", trials);

  for (double x = lo; x <= hi + 1e-6; x += step) {
    int acq[16] = {0}, del[16] = {0}, uni = 0;
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
      double ia = sqrt(ps) * pow(10.0, imp_db / 20.0);
      int imp_left = 0;
      for (int i = 0; i < ntot; i++) {
        double s = 0.0;
        int bi = i - lead;
        if (bi >= 0 && bi < n) s = burst[bi];
        /* the fading channel already added its noise inside the burst */
        if (!ch || bi < 0 || bi >= n) s += sigma * gauss();
        else (void)gauss();
        if (ta > 0.0) s += ta * cos(2.0 * M_PI * tone_hz * i / FS + tph);
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
        bench_run_burst(rx[k], buf, ntot, 0, 0.0, out, zero_noise, NULL, &ss, &got, NULL, NULL);
        if (got && memcmp(out, payload, (size_t)nbytes - 2)) got = 0;
        acq[k] += ss; del[k] += got; any |= got;
      }
      uni += any;
    }
    if (ch) chanutil_close(ch);
    printf("%+7.1f  %+7.2f ", x, meas_sum / trials);
    for (int k = 0; k < nv; k++) printf("    %3d/%-3d  ", acq[k], del[k]);
    printf("   any %3d\n", uni);
    fflush(stdout);
  }
  for (int k = 0; k < nv; k++) freedv_close(rx[k]);
  freedv_close(tx);
  free(burst); free(buf); free(payload); free(out);
  return 0;
}
