/* deep_floor_sim -- how far below the noise can a constant-envelope,
 * very-large-alphabet waveform go on the HF channel, and which one?
 *
 * Research prototype for docs/improvements/BELOW-NOISE-RESEARCH.md.  Nothing
 * in the modem uses it.
 *
 * The question.  A signal is "below the noise" when its power is less than
 * the noise power in the reference bandwidth (3 kHz here).  That is not a
 * limit: what decides is the energy the receiver can gather per bit against
 * the noise density (Eb/N0 >= -1.6 dB, Shannon), and that energy grows with
 * time.  A Morse operator copies CW under the noise because the ear is a
 * narrow filter and the brain integrates over time.  The waveform that is
 * "most different from noise" for a given energy puts that energy into ONE of
 * a large number of orthogonal shapes the receiver knows: one of M tones held
 * long enough (M-FSK), or one of M cyclic shifts of a chirp (chirp spread
 * spectrum, as LoRa does in the IoT bands and HF chirp sounders do for
 * ionospheric soundings).  As M grows, even noncoherent detection of such
 * orthogonal signals approaches the Shannon limit.  On HF the symbol cannot
 * be longer than the channel stays put (Doppler) nor wider than the
 * passband, so M <= bandwidth x symbol time, about 256 for 2 kHz and 128 ms.
 *
 * What this measures.  Frames of M-ary symbols carrying one of the modem's
 * own LDPC codewords, sent through the project's Watterson channel model
 * (common/watterson.c, the presets of watterson_presets()) and white noise,
 * decoded with the modem's own LDPC decoder.  Waveforms, same M, symbol time,
 * band, code, frame and channel realisation (paired):
 *
 *   fsk   one of M tones spaced 1/T (constant envelope, continuous phase)
 *   css   one of M cyclic shifts of a chirp sweeping the band in T
 *         (constant envelope); dechirp and FFT, energy in one bin
 *   cssr  css, and the receiver adds the energy of the bins where the
 *         channel's delayed paths land (a noncoherent RAKE): with a chirp, a
 *         path delayed by tau dechirps tau x B bins lower, so multipath
 *         becomes diversity instead of interference.  The path profile is
 *         measured on the frame's preamble (known chirps).
 *   cssg  css with the RAKE told the preset's path delays (upper bound)
 *
 * Symbol metrics are the noncoherent Rayleigh-fading log-likelihoods
 * (|Y|^2/s2 x g/(1+g), g the mean SNR per path, measured on the frame), bit
 * LLRs by log-sum-exp over the alphabet, a random interleaver over the
 * codeword.  Copies of a frame (a repeat, as the carousel could send) are
 * combined by adding the symbol log-likelihoods.
 *
 * Synchronisation is a genie by default (the receiver knows where the frame
 * starts and that there is no frequency offset): this measures the waveform
 * and the code.  `sync` adds a measurement of whether a preamble search
 * would have found the frame: the noncoherent sum of the preamble's dechirped
 * (css) or tone (fsk) energy at the true time and offset against the largest
 * over every other time (steps of T/8 over +-2 s) and every bin, per copy and
 * integrated over the copies (the receiver knows when a repeat is due).
 *
 * Baseband simulation at 2 kHz (the occupied band), so no transmitter or
 * receiver filter edges: optimistic by those, equally for every waveform.
 *
 * Environment (bench comparisons):
 *   DFS_METRIC=awgn     ln I0 symbol metric instead of the fading one
 *   DFS_NORM=sym        each symbol's bins divided by its own median bin
 *                       (robust to impulses; see g_norm_sym)
 *   DFS_NORM=bin|both   each bin divided by its own median over the frame
 *                       (robust to a narrow foreign signal; see g_norm_bin)
 *   DFS_IMP=rate,dB     impulses: rate per second, 2 ms each, dB over the signal
 *   DFS_FSK=Hz,dB[,baud,shift]  a 2-FSK foreign signal, offset from the
 *                       band's centre, dB over the signal's power
 *   DFS_SYNC=spread     known symbols at the start, middle and end (3+2+3)
 *                       instead of a preamble
 *   DFS_SYNCSTAT=energy the plain energy sum as the search metric instead of
 *                       the bounded log-odds (see g_sync_energy)
 *   DFS_SYNC_G=g        mean SNR per symbol the search metric assumes (4)
 *   DFS_DEBUG=1         print what beat the true alignment on a sync miss
 *
 * Usage:
 *   deep_floor_sim WAVES M CODE CHAN snr_lo snr_hi step trials [copies [sync]]
 *     WAVES  comma list of fsk, css, cssr, cssg
 *     M      alphabet size: 64, 128 or 256 (symbol time M / 2000 s)
 *     CODE   an LDPC code of modem/freedv/ldpc_codes.c (H_256_512_4,
 *            H_256_768_22, H_1024_2048_4f, ...)
 *     CHAN   awgn or a preset of watterson_presets() (ccir-poor, itur-mdv,...)
 *     snr    SNR in 3 kHz, dB
 *     copies frames combined per trial (default 1)
 *     sync   1 to add the preamble-search measurement (default 0)
 *
 * Copyright (C) 2026 Rhizomatica
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comp.h"
#include "kiss_fft.h"
#include "ldpc_codes.h"
#include "mpdecode_core.h"
#include "watterson.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FSB 2000.0 /* baseband sample rate = occupied bandwidth, Hz */
#define NPRE 8     /* preamble: known symbols */
#define MAXD 24    /* RAKE window in bins (12 ms at 2 kHz) */
#define MAXCOPIES 16

static uint64_t rng_s = 0x9E3779B97F4A7C15ULL;
static double uni(void) {
  rng_s ^= rng_s >> 12;
  rng_s ^= rng_s << 25;
  rng_s ^= rng_s >> 27;
  return (double)((rng_s * 2685821657736338717ULL) >> 11) / 9007199254740992.0;
}
static double gauss(void) {
  double u = uni(), v = uni();
  if (u < 1e-300) u = 1e-300;
  return sqrt(-2.0 * log(u)) * cos(2.0 * M_PI * v);
}

enum { W_FSK, W_CSS, W_CSSR, W_CSSG };
static const char *wname[] = {"fsk", "css", "cssr", "cssg"};

typedef struct {
  int M, bps, nd, nsym;         /* alphabet, bits/symbol, data symbols, symbols/frame */
  struct LDPC ldpc;
  int n, k;                     /* codeword, info bits */
  int *perm;                    /* interleaver: codeword bit perm[i] goes to slot i */
  int presym[NPRE];             /* the preamble's symbols */
  int spos[NPRE];               /* where in the frame the known symbols sit */
  int *dpos;                    /* ...and the data symbols */
  COMP *chirp;                  /* base up-chirp, M samples */
  kiss_fft_cfg fft;
  const watterson_preset_t *preset; /* NULL: AWGN */
} sim_t;

/* Base chirp at critical sampling: frequency from -B/2 to +B/2 in M samples.
 * A cyclic shift by k dechirps (times the conjugate base) to a pure tone in
 * bin k, with the same phase on both sides of the wrap (M integer). */
static void make_chirp(sim_t *s) {
  s->chirp = malloc(sizeof(COMP) * (size_t)s->M);
  for (int m = 0; m < s->M; m++) {
    double ph = M_PI * (double)m * (double)(m - s->M) / (double)s->M;
    s->chirp[m].real = (float)cos(ph);
    s->chirp[m].imag = (float)sin(ph);
  }
}

static void symbol(const sim_t *s, int wave, int k, COMP *out) {
  int M = s->M;
  if (wave == W_FSK) {
    for (int n = 0; n < M; n++) {
      double ph = 2.0 * M_PI * (double)(k - M / 2) * n / M;
      out[n].real = (float)cos(ph);
      out[n].imag = (float)sin(ph);
    }
  } else {
    for (int n = 0; n < M; n++) out[n] = s->chirp[(n + k) % M];
  }
}

/* Power spectrum of one received symbol, in the waveform's own bins: for
 * css, bin k holds symbol k's first path; for fsk, index k holds tone k. */
static void symbol_power(const sim_t *s, int wave, const COMP *y, float *p) {
  int M = s->M;
  kiss_fft_cpx in[1024] = {{0, 0}}, out[1024];
  for (int n = 0; n < M; n++) {
    if (wave == W_FSK) {
      in[n].r = y[n].real;
      in[n].i = y[n].imag;
    } else { /* times conj(chirp) */
      const COMP c = s->chirp[n];
      in[n].r = y[n].real * c.real + y[n].imag * c.imag;
      in[n].i = y[n].imag * c.real - y[n].real * c.imag;
    }
  }
  kiss_fft(s->fft, in, out);
  for (int b = 0; b < M; b++) {
    int k = (wave == W_FSK) ? (b + M / 2) % M : b;
    p[k] = out[b].r * out[b].r + out[b].i * out[b].i;
  }
}

/* ln I0(x), the noncoherent log-likelihood of a known-amplitude tone */
static double ln_i0(double x) {
  if (x < 3.75) {
    double t = x / 3.75, t2 = t * t;
    double i0 = 1.0 + t2 * (3.5156229 + t2 * (3.0899424 + t2 * (1.2067492 + t2 * (0.2659732 + t2 * (0.0360768 + t2 * 0.0045813)))));
    return log(i0);
  }
  return x - 0.5 * log(2.0 * M_PI * x) + log(1.0 + 1.0 / (8.0 * x) + 9.0 / (128.0 * x * x));
}
static int g_metric_awgn = 0; /* DFS_METRIC=awgn: ln I0 instead of the fading metric (bench comparison) */
/* DFS_NORM=sym: each symbol's bins are divided by that symbol's own median
 * bin (over ln 2) before the metric, instead of one noise level per frame.
 * The idea is from hearing (comodulation masking release): noise that rises
 * and falls across all frequencies at once, as an impulse or a static crash
 * does, is measured in the same instant at the frequencies where the signal
 * is not, and divided out.  A tone in one bin of 256 barely moves the
 * median, so the signal is not divided out with it. */
static int g_norm_sym = 0;
/* DFS_NORM=bin (or both): each bin divided by its own median over the
 * frame's data symbols: a narrow foreign signal that sits on a few bins for
 * the whole frame is then no louder than the noise there, instead of
 * looking like the transmitted symbol in every symbol (the noise-aware
 * weights of proposal 07, for the floor). */
static int g_norm_bin = 0;
/* DFS_FSK=offset_hz,dB[,baud,shift]: a 2-FSK foreign signal at that offset
 * from the band's centre, dB relative to the signal's power, added after
 * the channel (RTTY-like by default: 45.45 baud, 170 Hz) */
static double g_fsk_hz = 0, g_fsk_db = -999, g_fsk_baud = 45.45, g_fsk_shift = 170.0;
/* DFS_IMP=rate,dB: impulses, rate per second, each 2 ms of complex noise
 * dB above the signal's power, as utils/rx_bench adds them */
static double g_imp_rate = 0.0, g_imp_db = 0.0;

static int cmpf(const void *a, const void *b) {
  float x = *(const float *)a, y = *(const float *)b;
  return (x > y) - (x < y);
}

/* Symbol log-likelihoods of one received copy, added into ll[nd][M]. */
static void copy_metrics(const sim_t *s, int wave, const COMP *rx, double *ll, float *pw_scratch) {
  int M = s->M, nsym = s->nsym;
  /* power spectra of every symbol of the frame */
  for (int q = 0; q < nsym; q++) symbol_power(s, wave, rx + (size_t)q * M, pw_scratch + (size_t)q * M);
  /* noise per bin: the median bin power of the data symbols over ln 2 (the
   * signal sits in a handful of the M bins) */
  int nn = s->nd * M;
  float *tmp = malloc(sizeof(float) * (size_t)nn);
  for (int q = 0; q < s->nd; q++) memcpy(tmp + (size_t)q * M, pw_scratch + (size_t)s->dpos[q] * M, sizeof(float) * (size_t)M);
  qsort(tmp, (size_t)nn, sizeof(float), cmpf);
  double s2 = tmp[nn / 2] / M_LN2;
  free(tmp);
  if (s2 <= 0) s2 = 1e-12;
  if (g_norm_bin) {
    float *col = malloc(sizeof(float) * (size_t)s->nd);
    for (int b = 0; b < M; b++) {
      for (int q = 0; q < s->nd; q++) col[q] = pw_scratch[(size_t)s->dpos[q] * M + b];
      qsort(col, (size_t)s->nd, sizeof(float), cmpf);
      double sb = col[s->nd / 2] / M_LN2;
      /* only bins well above the frame's noise are brought down: the median
       * of 96 exponential samples scatters by about 15 %, and scaling every
       * bin by its own scatter cost about 1 dB at the cliff (measured) */
      if (sb <= 2.0 * s2) continue;
      double f = s2 / sb;
      for (int q = 0; q < nsym; q++) pw_scratch[(size_t)q * M + b] = (float)(pw_scratch[(size_t)q * M + b] * f);
    }
    free(col);
  }
  if (g_norm_sym) {
    /* per-symbol noise level: the symbol's median bin; scale it to the
     * frame's level so the rest of the metric is unchanged */
    float t2[1024];
    for (int q = 0; q < nsym; q++) {
      float *p = pw_scratch + (size_t)q * M;
      memcpy(t2, p, sizeof(float) * (size_t)M);
      qsort(t2, (size_t)M, sizeof(float), cmpf);
      double sq = t2[M / 2] / M_LN2;
      if (sq <= 0) continue;
      double f = s2 / sq;
      for (int b = 0; b < M; b++) p[b] = (float)(p[b] * f);
    }
  }
  /* mean SNR per symbol from the total energy (unbiased) */
  double etot = 0;
  for (int q = 0; q < s->nd; q++)
    for (int b = 0; b < M; b++) etot += pw_scratch[(size_t)s->dpos[q] * M + b];
  double g = etot / s->nd / s2 - M;
  if (g < 0.01) g = 0.01;

  /* the RAKE's per-path weights g_d/(1+g_d), d bins below the first path */
  double w[MAXD + 1];
  int nw = 1;
  w[0] = g / (1.0 + g);
  if (wave == W_CSSR) {
    /* measured on the preamble (all symbol presym[], known) */
    double prof[MAXD + 1] = {0};
    for (int q = 0; q < NPRE; q++)
      for (int d = 0; d <= MAXD; d++) prof[d] += pw_scratch[(size_t)s->spos[q] * M + ((s->presym[q] - d) % M + M) % M];
    double gsum = 0;
    for (int d = 0; d <= MAXD; d++) {
      double gd = prof[d] / NPRE / s2 - 1.0;
      /* a bin counts as a path only well above the noise of an 8-symbol
       * average (its standard deviation is 1/sqrt(8) of s2) */
      if (gd < 3.0 / sqrt((double)NPRE)) gd = 0.0;
      prof[d] = gd;
      gsum += gd;
    }
    if (gsum <= 0) { prof[0] = g; gsum = g; }
    /* scale the profile to the frame's mean SNR (the preamble is 1 s of a
     * fading channel), then the weights */
    for (int d = 0; d <= MAXD; d++) {
      double gd = prof[d] * g / gsum;
      w[d] = gd / (1.0 + gd);
    }
    nw = MAXD + 1;
  } else if (wave == W_CSSG && s->preset) {
    for (int d = 0; d <= MAXD; d++) w[d] = 0;
    double gs = 0;
    for (int p = 0; p < s->preset->npaths; p++) gs += s->preset->path[p].gain * s->preset->path[p].gain;
    for (int p = 0; p < s->preset->npaths; p++) {
      int d = (int)lrint(s->preset->path[p].delay_ms * FSB / 1000.0);
      if (d > MAXD) continue;
      double gd = g * s->preset->path[p].gain * s->preset->path[p].gain / gs;
      w[d] += gd / (1.0 + gd);
    }
    nw = MAXD + 1;
  }
  if (g_metric_awgn && nw == 1) {
    for (int q = 0; q < s->nd; q++) {
      const float *p = pw_scratch + (size_t)s->dpos[q] * M;
      double *l = ll + (size_t)q * M;
      for (int k = 0; k < M; k++) l[k] += ln_i0(2.0 * sqrt(g * p[k] / s2));
    }
    return;
  }
  for (int q = 0; q < s->nd; q++) {
    const float *p = pw_scratch + (size_t)s->dpos[q] * M;
    double *l = ll + (size_t)q * M;
    for (int k = 0; k < M; k++) {
      double z = 0;
      for (int d = 0; d < nw; d++)
        if (w[d] > 0) z += w[d] * p[((k - d) % M + M) % M];
      l[k] += z / s2;
    }
  }
}

/* Preamble search statistic of one copy: the noncoherent sum over the
 * preamble symbols of the energy at the true alignment, and the largest over
 * every other alignment (steps of M/8 samples over +-2 s, every bin).  Both
 * normalised by the noise per bin.  Added into acc[0] (true) and into
 * acc_false[j] for each false alignment j, so copies can be integrated. */
static int nalign(const sim_t *s) { return (int)(2.0 * 2.0 * FSB / (s->M / 8)) + 1; }
/* DFS_SYNCSTAT=energy: the plain sum of the known symbols' energies.  The
 * default bounds what one window can add: each window adds the posterior
 * log-odds that its tone is the expected one rather than any of the M
 * (ell_expected - logsumexp(ell) + ln M, at most ln M).  The plain sum lets
 * one strong DATA tone that happens to sit at the searched bin outweigh the
 * whole preamble under fading (measured: 10 % of searches missed on CCIR
 * Poor even at -12 dB).  The bound is the radar M-of-N idea, and LIGO's
 * chi-square veto, in soft form: a real sync is several windows agreeing, a
 * false one is a single loud window. */
static int g_sync_energy = 0;
static double g_sync_g = 4.0; /* mean SNR per symbol assumed by the search metric */
static void sync_stats(const sim_t *s, int wave, const COMP *buf, int start, double s2, double *acc_true,
                       double *acc_false) {
  int M = s->M, step = M / 8, na = nalign(s);
  float p[1024];
  COMP y[1024];
  int j = 0;
  for (int a = 0; a < na; a++) {
    int off = start + (a - na / 2) * step;
    /* alignments within one symbol of the truth catch most of the preamble
     * too (a chirp's bin follows the time error): those are near misses a
     * fine search would refine, not false alarms.  Leave them out. */
    if (a != na / 2 && abs(a - na / 2) < 8) continue;
    /* sum of the preamble's energies at each candidate bin offset: a time
     * error of t samples moves a chirp's bin by t, a frequency error moves
     * every bin; the search takes the best bin offset */
    double sum[1024] = {0};
    for (int q = 0; q < NPRE; q++) {
      memcpy(y, buf + off + (size_t)s->spos[q] * M, sizeof(COMP) * (size_t)M);
      symbol_power(s, wave, y, p);
      if (g_sync_energy) {
        for (int b = 0; b < M; b++) sum[b] += p[(s->presym[q] + b) % M];
      } else {
        double c = g_sync_g / (1.0 + g_sync_g), mx = 0, lse = 0;
        for (int k = 0; k < M; k++)
          if (p[k] * c / s2 > mx) mx = p[k] * c / s2;
        for (int k = 0; k < M; k++) lse += exp(p[k] * c / s2 - mx);
        lse = mx + log(lse);
        /* scaled by s2 so the caller's division gives the log-odds */
        for (int b = 0; b < M; b++) sum[b] += s2 * (p[(s->presym[q] + b) % M] * c / s2 - lse + log((double)M));
      }
    }
    if (a == na / 2) {
      /* the true alignment: the frame's own bin or the bin of one of its
       * delayed paths (a chirp's path tau late dechirps tau x B bins lower),
       * or a bin or two of Doppler; the best of them is what a search finds */
      double best = 0;
      for (int b = 0; b < M; b++) {
        int near = (b <= 2) || (b >= M - MAXD);
        if (near) {
          if (sum[b] > best) best = sum[b];
        } else
          acc_false[j++] += sum[b] / s2;
      }
      acc_true[0] += best / s2;
    } else {
      for (int b = 0; b < M; b++) acc_false[j++] += sum[b] / s2;
    }
  }
}

int main(int argc, char **argv) {
  if (argc < 9) {
    fprintf(stderr, "usage: %s WAVES M CODE CHAN snr_lo snr_hi step trials [copies [sync]]\n", argv[0]);
    return 2;
  }
  int waves[4], nwav = 0;
  {
    char *cp = strdup(argv[1]), *t = strtok(cp, ",");
    while (t && nwav < 4) {
      int found = -1;
      for (int i = 0; i < 4; i++)
        if (!strcmp(t, wname[i])) found = i;
      if (found < 0) { fprintf(stderr, "unknown waveform %s\n", t); return 2; }
      waves[nwav++] = found;
      t = strtok(NULL, ",");
    }
    free(cp);
  }
  sim_t s;
  memset(&s, 0, sizeof(s));
  g_metric_awgn = getenv("DFS_METRIC") && !strcmp(getenv("DFS_METRIC"), "awgn");
  g_norm_sym = getenv("DFS_NORM") && (!strcmp(getenv("DFS_NORM"), "sym") || !strcmp(getenv("DFS_NORM"), "both"));
  g_norm_bin = getenv("DFS_NORM") && (!strcmp(getenv("DFS_NORM"), "bin") || !strcmp(getenv("DFS_NORM"), "both"));
  if (getenv("DFS_FSK")) sscanf(getenv("DFS_FSK"), "%lf,%lf,%lf,%lf", &g_fsk_hz, &g_fsk_db, &g_fsk_baud, &g_fsk_shift);
  g_sync_energy = getenv("DFS_SYNCSTAT") && !strcmp(getenv("DFS_SYNCSTAT"), "energy");
  if (getenv("DFS_SYNC_G")) g_sync_g = atof(getenv("DFS_SYNC_G"));
  if (getenv("DFS_IMP")) sscanf(getenv("DFS_IMP"), "%lf,%lf", &g_imp_rate, &g_imp_db);
  s.M = atoi(argv[2]);
  if (s.M != 64 && s.M != 128 && s.M != 256 && s.M != 512) { fprintf(stderr, "M must be 64..512\n"); return 2; }
  s.bps = (int)lrint(log2((double)s.M));
  ldpc_codes_setup(&s.ldpc, argv[3]);
  s.n = s.ldpc.CodeLength;
  s.k = s.n - s.ldpc.NumberParityBits;
  s.nd = (s.n + s.bps - 1) / s.bps;
  s.nsym = NPRE + s.nd;
  const char *chan = argv[4];
  double lo = atof(argv[5]), hi = atof(argv[6]), step = atof(argv[7]);
  int trials = atoi(argv[8]);
  int copies = argc > 9 ? atoi(argv[9]) : 1;
  int dosync = argc > 10 ? atoi(argv[10]) : 0;
  if (copies < 1 || copies > MAXCOPIES) { fprintf(stderr, "copies 1..%d\n", MAXCOPIES); return 2; }
  s.preset = strcmp(chan, "awgn") ? watterson_preset_find(chan) : NULL;
  if (strcmp(chan, "awgn") && !s.preset) { fprintf(stderr, "unknown channel %s\n", chan); return 2; }
  make_chirp(&s);
  /* Known symbols: all at the start (a preamble), or with DFS_SYNC=spread
   * in three groups, at the start, the middle and the end (3 + 2 + 3), as
   * FT8 spreads its Costas arrays: a fade over the start then does not take
   * the whole synchronisation with it. */
  {
    int spread = getenv("DFS_SYNC") && !strcmp(getenv("DFS_SYNC"), "spread");
    int mark[4096] = {0};
    for (int q = 0; q < NPRE; q++) {
      int pos = q;
      if (spread) pos = q < 3 ? q : q < 5 ? s.nsym / 2 - 1 + (q - 3) : s.nsym - 3 + (q - 5);
      s.spos[q] = pos;
      mark[pos] = 1;
    }
    s.dpos = malloc(sizeof(int) * (size_t)s.nd);
    int j = 0;
    for (int q = 0; q < s.nsym; q++)
      if (!mark[q]) s.dpos[j++] = q;
  }
  s.fft = kiss_fft_alloc(s.M, 0, NULL, NULL);
  s.perm = malloc(sizeof(int) * (size_t)(s.nd * s.bps));
  {
    int tot = s.nd * s.bps;
    for (int i = 0; i < tot; i++) s.perm[i] = i;
    uint64_t save = rng_s;
    rng_s = 12345;
    for (int i = tot - 1; i > 0; i--) {
      int j = (int)(uni() * (i + 1));
      int t = s.perm[i];
      s.perm[i] = s.perm[j];
      s.perm[j] = t;
    }
    for (int q = 0; q < NPRE; q++) s.presym[q] = (int)(uni() * s.M);
    rng_s = save;
  }

  double T = s.M / FSB;
  double frame_s = s.nsym * T;
  double rate = (s.k - 16) / (frame_s * copies); /* payload bits/s, CRC16 out */
  printf("M %d (%d bits, T %.0f ms, %.2f Hz bins, band %.0f Hz), code %s (n %d, k %d), %d data + %d preamble symbols, "
         "frame %.2f s, %d cop%s: %.1f payload bit/s\n",
         s.M, s.bps, 1000 * T, FSB / s.M, FSB, s.ldpc.name, s.n, s.k, s.nd, NPRE, frame_s, copies,
         copies > 1 ? "ies" : "y", rate);
  printf("channel %s%s, %d trials a point; Eb/N0 = SNR3k + %.1f dB\n", chan, s.preset ? "" : " (static path)", trials,
         10 * log10(3000.0 / rate));
  printf("SNR3k ");
  for (int w = 0; w < nwav; w++) printf(" %7s", wname[waves[w]]);
  if (dosync)
    for (int w = 0; w < nwav; w++) printf("  sync1:%-4s syncN:%-4s", wname[waves[w]], wname[waves[w]]);
  printf("   (frames delivered of %d%s)\n", trials, dosync ? "; sync: preamble found, one copy / all copies" : "");

  int fl = s.nsym * s.M;
  int guard = (int)(2.2 * FSB) + s.M; /* room for the sync search around the frame */
  int buflen = fl + 2 * guard;
  COMP *tx = malloc(sizeof(COMP) * (size_t)fl);
  COMP *buf = malloc(sizeof(COMP) * (size_t)buflen);
  double *ll = malloc(sizeof(double) * (size_t)(s.nd * s.M));
  float *pw = malloc(sizeof(float) * (size_t)(s.nsym * s.M));
  float *llr = malloc(sizeof(float) * (size_t)s.n);
  uint8_t *out = malloc((size_t)s.n);
  unsigned char *ib = malloc((size_t)s.k), *pb = malloc((size_t)s.n);
  int *sym = malloc(sizeof(int) * (size_t)s.nd);
  int na = nalign(&s);
  double *accf = malloc(sizeof(double) * (size_t)(na * s.M));

  for (double snr = lo; snr <= hi + 1e-9; snr += step) {
    int ok[4] = {0}, sy1[4] = {0}, syn[4] = {0};
    for (int wi = 0; wi < nwav; wi++) {
      int wave = waves[wi];
      /* the same channel, noise and data for every waveform: re-seed */
      rng_s = 0x9E3779B97F4A7C15ULL ^ (uint64_t)lrint((snr + 100) * 1000);
      watterson_t wch;
      watterson_init(&wch, (int)FSB);
      if (s.preset)
        watterson_apply_preset(&wch, s.preset);
      else
        watterson_add_path(&wch, 0.0f, 0.0f, 0.0f, 1.0f);
      watterson_set_noise(&wch, -200.0f);
      watterson_seed(&wch, 777u);
      { /* warm the fading process */
        int nwarm = (int)(20.0 * FSB);
        COMP *z = calloc((size_t)nwarm, sizeof(COMP));
        watterson_process(&wch, z, nwarm);
        free(z);
      }
      double sc2 = FSB / (pow(10.0, snr / 10.0) * 3000.0); /* complex noise per sample, unit signal power */
      double sc = sqrt(sc2 / 2.0);
      for (int t = 0; t < trials; t++) {
        for (int i = 0; i < s.k; i++) ib[i] = (unsigned char)(uni() < 0.5);
        encode(&s.ldpc, ib, pb + s.k);
        memcpy(pb, ib, (size_t)s.k);
        for (int q = 0; q < s.nd; q++) {
          int v = 0;
          for (int b = 0; b < s.bps; b++) {
            int slot = q * s.bps + b, cb = s.perm[slot];
            v = (v << 1) | (cb < s.n ? pb[cb] : 0);
          }
          sym[q] = v;
        }
        for (int q = 0; q < NPRE; q++) symbol(&s, wave, s.presym[q], tx + (size_t)s.spos[q] * s.M);
        for (int q = 0; q < s.nd; q++) symbol(&s, wave, sym[q], tx + (size_t)s.dpos[q] * s.M);
        memset(ll, 0, sizeof(double) * (size_t)(s.nd * s.M));
        double acct = 0;
        if (dosync) memset(accf, 0, sizeof(double) * (size_t)(na * s.M));
        int found1 = 0;
        for (int c = 0; c < copies; c++) {
          /* noise before and after, the frame in the middle, all through the
           * channel in one go so the fading runs on */
          memset(buf, 0, sizeof(COMP) * (size_t)buflen);
          memcpy(buf + guard, tx, sizeof(COMP) * (size_t)fl);
          watterson_process(&wch, buf, buflen);
          if (g_fsk_db > -900) {
            double fa = sqrt(pow(10.0, g_fsk_db / 10.0)), ph = 2.0 * M_PI * uni(), f = g_fsk_hz;
            long sym = -1;
            for (int i = 0; i < buflen; i++) {
              long ns = (long)floor((double)i * g_fsk_baud / FSB);
              if (ns != sym) { sym = ns; f = g_fsk_hz + (uni() < 0.5 ? -0.5 : 0.5) * g_fsk_shift; }
              buf[i].real += (float)(fa * cos(ph));
              buf[i].imag += (float)(fa * sin(ph));
              ph += 2.0 * M_PI * f / FSB;
              if (ph > 2.0 * M_PI) ph -= 2.0 * M_PI;
            }
          }
          int imp_left = 0;
          double ia = sqrt(pow(10.0, g_imp_db / 10.0) / 2.0); /* per component, unit signal power */
          for (int i = 0; i < buflen; i++) {
            buf[i].real += (float)(sc * gauss());
            buf[i].imag += (float)(sc * gauss());
            if (g_imp_rate > 0) {
              if (!imp_left && uni() < g_imp_rate / FSB) imp_left = (int)(0.002 * FSB) + 1;
              if (imp_left) {
                buf[i].real += (float)(ia * gauss());
                buf[i].imag += (float)(ia * gauss());
                imp_left--;
              }
            }
          }
          copy_metrics(&s, wave, buf + guard, ll, pw);
          if (dosync) {
            double s2 = (double)s.M * sc2; /* noise per bin, known here */
            double a1 = 0;
            double *af = calloc((size_t)(na * s.M), sizeof(double));
            sync_stats(&s, wave, buf, guard, s2, &a1, af);
            double mx = 0;
            for (int j = 0; j < na * s.M - 1; j++) {
              if (af[j] > mx) mx = af[j];
              accf[j] += af[j];
            }
            if (c == 0 && a1 > mx) found1 = 1;
            if (getenv("DFS_DEBUG") && c == 0 && a1 <= mx) { int jm = 0; for (int j = 0; j < na * s.M - 1; j++) if (af[j] == mx) jm = j; fprintf(stderr, "sync miss: true %.1f, false max %.1f at index %d (alignment %d of %d, bin %d)\n", a1, mx, jm, jm / s.M, na, jm % s.M); }
            acct += a1;
            free(af);
          }
        }
        if (dosync) {
          double mx = 0;
          for (int j = 0; j < na * s.M - 1; j++)
            if (accf[j] > mx) mx = accf[j];
          sy1[wi] += found1;
          syn[wi] += acct > mx;
        }
        /* bit LLRs (positive = 0), MSB first, then deinterleave */
        for (int i = 0; i < s.n; i++) llr[i] = 0;
        for (int q = 0; q < s.nd; q++) {
          const double *l = ll + (size_t)q * s.M;
          double m = l[0];
          for (int k2 = 1; k2 < s.M; k2++)
            if (l[k2] > m) m = l[k2];
          double e[1024];
          for (int k2 = 0; k2 < s.M; k2++) e[k2] = exp(l[k2] - m);
          for (int b = 0; b < s.bps; b++) {
            int bit = s.bps - 1 - b;
            double s0 = 1e-300, s1 = 1e-300;
            for (int k2 = 0; k2 < s.M; k2++) {
              if ((k2 >> bit) & 1)
                s1 += e[k2];
              else
                s0 += e[k2];
            }
            int slot = q * s.bps + b, cb = s.perm[slot];
            if (cb < s.n) {
              double v = log(s0) - log(s1);
              if (v > 60) v = 60;
              if (v < -60) v = -60;
              llr[cb] = (float)v;
            }
          }
        }
        int pcc = 0;
        run_ldpc_decoder(&s.ldpc, out, llr, &pcc);
        if (!memcmp(out, ib, (size_t)s.k)) ok[wi]++;
      }
      watterson_dispose(&wch);
    }
    printf("%+5.1f ", snr);
    for (int w = 0; w < nwav; w++) printf(" %7d", ok[w]);
    if (dosync)
      for (int w = 0; w < nwav; w++) printf("  %10d %10d", sy1[w], syn[w]);
    printf("\n");
    fflush(stdout);
  }
  return 0;
}
