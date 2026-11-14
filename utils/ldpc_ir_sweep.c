/* ldpc_ir_sweep -- how much redundancy does a frame need, and what would it
 * save to send it in instalments?
 *
 * Today a frame is all or nothing: a mode sends its whole codeword, and a
 * frame that fails its CRC is thrown away with everything it carried (inside
 * a carousel session the soft combiner is off: modem.c, modem_apply_crc_seed).
 * This tool measures, on the codes the modes already use and with the decoder
 * they already use, what the alternative is worth:
 *
 *   - send the data bits and only PART of the parity first (the punctured
 *     positions reach the decoder as zero LLR: "never seen");
 *   - when that fails, send MORE of the same codeword (the rest of the
 *     parity, then bits a second time), and decode again from everything
 *     received so far.
 *
 * Each trial draws one random payload, encodes it, and walks up a ladder of
 * "fraction of a codeword sent so far" until the decoder delivers the
 * payload.  The smallest fraction that decodes is the trial's result.  From
 * that one number per trial the tool derives, per noise level:
 *
 *   - P(decoded) at every fraction (the family of cliffs of one code);
 *   - bits put on the air per delivered payload bit for three senders:
 *       discard   whole codewords, a failed one is thrown away (today);
 *       repeat    whole codewords, failed copies are combined (proposal 04);
 *       instal.   start below a whole codeword and add until it decodes;
 *   - the same figure for an ideal code (binary-input AWGN capacity).
 *
 * This is the CODE alone: perfect synchronisation and channel knowledge,
 * white noise, one coded bit per real dimension (QPSK).  It says how the
 * cliffs of one code move with the amount sent, and what a sender that adds
 * redundancy on demand saves in airtime.  It says nothing about acquisition,
 * channel estimation or fading: those have to be measured with the whole
 * modem (utils/rx_bench) once there is something to measure.
 *
 * Order of sending: the data bits, then the parity bits in bit-reversed
 * order (any prefix is evenly spread over the parity part, so every level is
 * a regular puncturing pattern and the levels are nested), then all the bits
 * again, also in bit-reversed order.  "tail" instead sends the parity in its
 * natural order (the first level drops the END of the parity), to show what
 * a careless pattern costs.
 *
 * Self-checks: with a whole codeword the cliff must sit where the code's is
 * known to be; two whole copies must be 3.0 dB below one; no level may be
 * better than capacity.
 *
 * Usage: ldpc_ir_sweep CODE [trials [lo_db hi_db step_db [even|tail]]]
 *        CODE is a name of modem/freedv/ldpc_codes.c, e.g. H_4096_8192_3d.
 *        dB are Ec/N0: energy per SENT coded bit over the noise density.
 *
 * Copyright (C) 2026 Rhizomatica
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ldpc_codes.h"
#include "mpdecode_core.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static uint64_t rng_s = 88172645463325252ULL;
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

/* Capacity of the binary-input AWGN channel, bits per use, for symbols of
 * +-1 in noise of deviation sigma: 1 - E[log2(1 + exp(-L))], L the LLR. */
static double biawgn_capacity(double sigma) {
  double acc = 0.0, dz = 0.004;
  for (double z = -9.0; z <= 9.0; z += dz) {
    double l = 2.0 * (1.0 + sigma * z) / (sigma * sigma);
    double t;
    if (l > 40.0)
      t = exp(-l) / M_LN2;
    else if (l < -40.0)
      t = -l / M_LN2;
    else
      t = log2(1.0 + exp(-l));
    acc += exp(-0.5 * z * z) / sqrt(2.0 * M_PI) * t * dz;
  }
  return 1.0 - acc;
}
static double sigma_of_db(double ecn0_db) { return sqrt(1.0 / (2.0 * pow(10.0, ecn0_db / 10.0))); }
/* The Ec/N0 at which an ideal code of this rate starts to work. */
static double shannon_db(double rate) {
  double lo = -30.0, hi = 20.0;
  for (int i = 0; i < 60; i++) {
    double mid = 0.5 * (lo + hi);
    if (biawgn_capacity(sigma_of_db(mid)) < rate)
      lo = mid;
    else
      hi = mid;
  }
  return 0.5 * (lo + hi);
}

/* A permutation of 0..m-1 every prefix of which is evenly spread. */
static void bitrev_order(int *out, int m) {
  int nb = 0;
  while ((1 << nb) < m) nb++;
  int w = 0;
  for (int r = 0; r < (1 << nb); r++) {
    int v = 0;
    for (int b = 0; b < nb; b++)
      if (r & (1 << b)) v |= 1 << (nb - 1 - b);
    if (v < m) out[w++] = v;
  }
}

#define MAXLEV 24

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s CODE [trials [lo_db hi_db step_db [even|tail]]]\n", argv[0]);
    ldpc_codes_list();
    return 2;
  }
  int trials = (argc > 2) ? atoi(argv[2]) : 30;
  double lo = (argc > 3) ? atof(argv[3]) : -8.0;
  double hi = (argc > 4) ? atof(argv[4]) : 4.0;
  double step = (argc > 5) ? atof(argv[5]) : 0.5;
  int tail = (argc > 6) && !strcmp(argv[6], "tail");

  struct LDPC ldpc;
  memset(&ldpc, 0, sizeof(ldpc));
  ldpc_codes_setup(&ldpc, argv[1]);
  int n = ldpc.CodeLength, m = ldpc.NumberParityBits, k = n - m;
  double rate = (double)k / n;

  /* The ladder of fractions: from the data bits plus a fifth of the parity
   * up to four whole codewords. */
  double lev[MAXLEV];
  int nlev = 0;
  for (int i = 1; i <= 5; i++) lev[nlev++] = rate + (1.0 - rate) * i / 5.0; /* parity in fifths */
  static const double more[] = {1.125, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0, 4.0};
  for (unsigned i = 0; i < sizeof(more) / sizeof(more[0]); i++) lev[nlev++] = more[i];
  int whole = 4; /* index of the level "one whole codeword" */

  int *pord = malloc(sizeof(int) * (size_t)m), *rord = malloc(sizeof(int) * (size_t)n);
  if (tail)
    for (int i = 0; i < m; i++) pord[i] = i;
  else
    bitrev_order(pord, m);
  bitrev_order(rord, n);

  unsigned char *ib = malloc((size_t)k), *pb = malloc((size_t)m), *cw = malloc((size_t)n);
  uint8_t *out = malloc((size_t)n);
  float *llr = malloc(sizeof(float) * (size_t)n);

  printf("%s: n %d, k %d, rate %.3f, %d iterations, %d trials a point, parity sent %s\n", ldpc.name, n, k, rate,
         ldpc.max_iter, trials, tail ? "in its natural order (tail dropped first)" : "evenly spread");
  printf("levels (fraction of a codeword sent; effective rate; where an ideal code of that rate starts, Ec/N0 dB):\n");
  for (int j = 0; j < nlev; j++)
    printf("  %5.3f  rate %.3f  ideal %+6.2f\n", lev[j], rate / lev[j], shannon_db(rate / lev[j]));

  printf("\nP(decoded), per cent, by fraction sent; then airtime: coded bits sent per delivered payload bit\n");
  printf("Ec/N0  ");
  for (int j = 0; j < nlev; j++) printf("%5.2f ", lev[j]);
  printf("| ideal discard repeat instal(from %.2f) instal(from 1.00)\n", lev[1]);

  for (double db = lo; db <= hi + 1e-9; db += step) {
    double sigma = sigma_of_db(db);
    double cap = biawgn_capacity(sigma);
    int okat[MAXLEV];
    memset(okat, 0, sizeof(okat));
    /* airtime accounting, in codewords sent and payloads delivered */
    double sent_dis = 0, del_dis = 0, sent_rep = 0, del_rep = 0;
    double sent_i1 = 0, del_i1 = 0, sent_iw = 0, del_iw = 0;
    for (int t = 0; t < trials; t++) {
      for (int i = 0; i < k; i++) ib[i] = (unsigned char)(uni() < 0.5);
      encode(&ldpc, ib, pb);
      memcpy(cw, ib, (size_t)k);
      memcpy(cw + k, pb, (size_t)m);
      memset(llr, 0, sizeof(float) * (size_t)n);
      long pos = 0;
      int first = -1; /* the first level that decodes */
      for (int j = 0; j < nlev && first < 0; j++) {
        long upto = (long)floor(lev[j] * n + 0.5);
        for (; pos < upto; pos++) {
          int idx = (pos < k) ? (int)pos : (pos < n) ? k + pord[pos - k] : rord[(pos - n) % n];
          double y = (cw[idx] ? -1.0 : 1.0) + sigma * gauss();
          llr[idx] += (float)(2.0 * y / (sigma * sigma));
        }
        /* nothing decodes with less information than the payload holds:
         * skip the attempt (saves time, changes no result) */
        if ((double)upto * cap < 0.93 * k) continue;
        /* Delivered = the payload bits are right, which is what the frame's
         * CRC decides in the modem.  Not "every parity check satisfied": the
         * decoder counts checks from extrinsic signs, and a check on a bit
         * that was never sent (or on the last, degree-1, parity bit) can stay
         * unsatisfied with every bit right. */
        int pcc = 0;
        run_ldpc_decoder(&ldpc, out, llr, &pcc);
        if (!memcmp(out, ib, (size_t)k)) first = j;
      }
      for (int j = 0; j < nlev; j++)
        if (first >= 0 && j >= first) okat[j]++;
      /* discard: one whole codeword, delivered or lost */
      sent_dis += 1.0;
      if (first >= 0 && first <= whole) del_dis += 1.0;
      /* repeat: whole codewords, combined, up to four */
      if (first >= 0) {
        double f = lev[first] <= 1.0 ? 1.0 : ceil(lev[first] - 1e-9);
        sent_rep += f;
        del_rep += 1.0;
        /* instalments from the second level, and from a whole codeword */
        sent_i1 += lev[first] > lev[1] ? lev[first] : lev[1];
        del_i1 += 1.0;
        sent_iw += lev[first] > 1.0 ? lev[first] : 1.0;
        del_iw += 1.0;
      } else {
        sent_rep += 4.0;
        sent_i1 += 4.0;
        sent_iw += 4.0;
      }
    }
    printf("%+5.1f  ", db);
    for (int j = 0; j < nlev; j++) printf("%5.0f ", 100.0 * okat[j] / trials);
    printf("| %5.2f", 1.0 / cap);
    /* coded bits per payload bit = codewords * n / (payloads * k) */
    double v[4] = {del_dis > 0 ? sent_dis / del_dis / rate : 0, del_rep > 0 ? sent_rep / del_rep / rate : 0,
                   del_i1 > 0 ? sent_i1 / del_i1 / rate : 0, del_iw > 0 ? sent_iw / del_iw / rate : 0};
    for (int q = 0; q < 4; q++) {
      if (v[q] > 0)
        printf(" %6.2f", v[q]);
      else
        printf("      -");
    }
    printf("\n");
    fflush(stdout);
  }
  free(pord);
  free(rord);
  free(ib);
  free(pb);
  free(cw);
  free(out);
  free(llr);
  return 0;
}
