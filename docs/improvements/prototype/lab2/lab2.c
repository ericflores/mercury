/* lab2 -- scratch harness for the "more compute" experiments.
 * variants: tokens joined by '+':  stock | t4 | t4f3 | t4f5 | cp1 | cp2 | cp3 | ensN | itN
 * PIECES=1 prints, for frames that failed, how many 24-byte pieces of the
 * decoder's output were nevertheless entirely right. */
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
extern int mx_cprec, mx_ens, mx_iter, mx_fail_n, mx_fail_pcc, mx_fail_npar, mx_genie;
extern unsigned char mx_fail_bytes[];
extern float mx_ens_sigma;
static bench_rng rng = {1357911131517ULL};
static double gauss(void) { return bench_rng_gauss(&rng); }
typedef struct { int chanest, cp, ens, iter, genie; char name[40]; } variant_t;

int main(int argc, char **argv) {
  if (argc < 7) { fprintf(stderr, "usage: lab2 MODE trials lo hi step chan [variants]\n"); return 1; }
  int mode = bench_mode_from_name(argv[1]);
  int trials = atoi(argv[2]);
  double lo = atof(argv[3]), hi = atof(argv[4]), step = atof(argv[5]);
  int chan = chanutil_preset_from_name(argv[6]);
  const char *vlist = argc > 7 ? argv[7] : "stock";
  int pieces = getenv("PIECES") != NULL;
  if (getenv("ENS_SIGMA")) mx_ens_sigma = (float)atof(getenv("ENS_SIGMA"));
  variant_t v[16]; int nv = 0; char tmp[400];
  strncpy(tmp, vlist, sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = 0;
  for (char *t = strtok(tmp, ","); t && nv < 16; t = strtok(NULL, ",")) {
    memset(&v[nv], 0, sizeof v[nv]);
    strncpy(v[nv].name, t, 39);
    {   /* exact tokens joined by '+': a substring match once read "it400" as "t4" */
      char one[40]; strncpy(one, t, 39); one[39] = 0;
      for (char *q = one, *e; q && *q; q = e) {
        e = strchr(q, '+'); if (e) *e++ = 0;
        if (!strcmp(q, "stock")) ;
        else if (!strcmp(q, "t4f5")) v[nv].chanest = OFDM_CHANEST_T4F5;
        else if (!strcmp(q, "t4f3")) v[nv].chanest = OFDM_CHANEST_T4F3;
        else if (!strcmp(q, "t4")) v[nv].chanest = OFDM_CHANEST_T4;
        else if (!strncmp(q, "genie", 5)) { v[nv].genie = atoi(q + 5) ? atoi(q + 5) : 1; if (!v[nv].chanest) v[nv].chanest = OFDM_CHANEST_T4F5; }
        else if (!strncmp(q, "cp", 2)) v[nv].cp = atoi(q + 2);
        else if (!strncmp(q, "ens", 3)) v[nv].ens = atoi(q + 3);
        else if (!strncmp(q, "mi", 2)) v[nv].iter = atoi(q + 2);
        else { fprintf(stderr, "unknown token '%s'\n", q); return 1; }
      }
    }
    nv++;
  }
  struct freedv *tx = freedv_open(mode), *rx[16];
  for (int k = 0; k < nv; k++) {
    rx[k] = freedv_open(mode);
    freedv_set_frames_per_burst(rx[k], 1);
    freedv_set_chanest_alt(rx[k], v[k].chanest);
  }
  freedv_set_frames_per_burst(tx, 1);
  int nbytes = freedv_get_bits_per_modem_frame(tx) / 8;
  int nburst = freedv_get_n_tx_preamble_modem_samples(tx) + freedv_get_n_tx_modem_samples(tx) +
               freedv_get_n_tx_postamble_modem_samples(tx);
  int lead = (int)(0.4 * FS), ntot = lead + nburst + nburst;
  short *burst = malloc(sizeof(short) * (size_t)(nburst + 4)), *buf = malloc(sizeof(short) * (size_t)ntot);
  unsigned char *payload = malloc((size_t)nbytes), *out = malloc((size_t)nbytes + 8), *last = malloc((size_t)nbytes + 8);
  printf("%s %d B, chan %s\n%-8s %-8s", argv[1], nbytes, chanutil_preset_name(chan), chan == CHAN_AWGN ? "SNR3k" : "No", "meas");
  for (int k = 0; k < nv; k++) printf(" %14s", v[k].name);
  printf("   (acquired/delivered per %d)\n", trials);
  for (double x = lo; x <= hi + 1e-6; x += step) {
    int acq[16] = {0}, del[16] = {0}, uni = 0;
    long pc_tot = 0, pc_ok = 0, fr_fail = 0, biterr = 0, bits = 0; int hist[8] = {0}; double pcc_sum = 0.0;
    double meas_sum = 0.0;
    chanutil_t *ch = chan != CHAN_AWGN ? chanutil_open(chan, (float)x, 1234u) : NULL;
    for (int t = 0; t < trials; t++) {
      bench_fill_payload(payload, nbytes, &rng);
      int n = bench_make_burst(tx, burst, payload);
      double ps = 0.0, pk = 0.0;
      for (int i = 0; i < n; i++) { ps += (double)burst[i] * burst[i]; if (fabs((double)burst[i]) > pk) pk = fabs((double)burst[i]); }
      ps /= n; double gn = HEADROOM_PEAK / pk;
      for (int i = 0; i < n; i++) burst[i] = (short)lrint(burst[i] * gn);
      ps *= gn * gn; double snr = x;
      if (ch) { float m = 0.0f; chanutil_run(ch, (int16_t *)burst, n, &m); snr = m; }
      meas_sum += snr;
      double sigma = sqrt(ps / (pow(10.0, snr / 10.0) * (3000.0 / (FS / 2.0))));
      for (int i = 0; i < ntot; i++) {
        double s = 0.0; int bi = i - lead;
        if (bi >= 0 && bi < n) s = burst[bi];
        if (!ch || bi < 0 || bi >= n) s += sigma * gauss(); else (void)gauss();
        if (s > 32767.0) s = 32767.0; if (s < -32768.0) s = -32768.0;
        buf[i] = (short)lrint(s);
      }
      int any = 0;
      for (int k = 0; k < nv; k++) {
        mx_genie = v[k].genie; mx_cprec = v[k].cp; mx_ens = v[k].ens; mx_iter = v[k].iter ? v[k].iter : 100;
        int pos = 0, ss = 0, got = 0, havefail = 0; short chunk[8192]; mx_fail_n = 0;
        freedv_set_sync(rx[k], FREEDV_SYNC_UNSYNC);
        while (pos < ntot) {
          int nin = freedv_nin(rx[k]); if (nin > 8192) break;
          for (int i = 0; i < nin; i++) chunk[i] = pos + i < ntot ? buf[pos + i] : 0;
          pos += nin;
          int nb = (int)freedv_rawdatarx(rx[k], out, chunk);
          int st = freedv_get_rx_status(rx[k]);
          if (st & FREEDV_RX_SYNC) ss = 1;
          if (nb > 0 && !(st & FREEDV_RX_BIT_ERRORS) && !memcmp(out, payload, (size_t)nbytes - 2)) got = 1;
        }
        acq[k] += ss; del[k] += got; any |= got;
        if (!got && mx_fail_n >= nbytes) { memcpy(last, mx_fail_bytes, (size_t)nbytes); havefail = 1; pcc_sum += (double)mx_fail_pcc / (mx_fail_npar ? mx_fail_npar : 1); }
        if (pieces && k == 0 && !got && havefail) {
          int np = (nbytes - 2) / 24, okp = 0;
          for (int p = 0; p < np; p++) if (!memcmp(last + p * 24, payload + p * 24, 24)) okp++;
          for (int i = 0; i < nbytes - 2; i++) { unsigned d = last[i] ^ payload[i]; while (d) { biterr += d & 1; d >>= 1; } }
          bits += (nbytes - 2) * 8; pc_tot += np; pc_ok += okp; fr_fail++;
          int frac = np ? (okp * 7) / np : 0; hist[frac]++;
        }
      }
      uni += any;
    }
    if (ch) chanutil_close(ch);
    printf("%+7.1f  %+7.2f ", x, meas_sum / trials);
    for (int k = 0; k < nv; k++) printf("      %3d/%-3d  ", acq[k], del[k]);
    printf("   any %3d\n", uni);
    if (pieces && fr_fail)
      printf("         failed frames (stock): %ld; residual BER %.4f; pieces fully right %ld/%ld (%.1f%%); frames by share right [0..1]: %d %d %d %d %d %d %d %d\n",
             fr_fail, bits ? (double)biterr / bits : 0.0, pc_ok, pc_tot, pc_tot ? 100.0 * pc_ok / pc_tot : 0.0,
             hist[0], hist[1], hist[2], hist[3], hist[4], hist[5], hist[6], hist[7]);
    fflush(stdout);
  }
  return 0;
}
