/* rxlab -- scratch experiment harness (not for merging as is).
 *
 * One transmitted burst per trial, one impaired audio buffer, decoded by
 * several receiver variants that all see the SAME samples.
 *
 *   rxlab MODE trials lo hi step chan [variants] [tone_hz tone_db] [imp_rate imp_db]
 *
 *   chan      awgn (lo/hi are SNR3k) or a watterson preset (lo/hi are No)
 *   variants  comma list of  c<chanest>n<noisew>b<blanker k>s<esnodb>, e.g. c0,c7,c0n1,c0b6,c0s6
 *   tone      a CW interferer at tone_hz, tone_db relative to the signal power
 *   imp       impulse noise: imp_rate bursts/s of 2 ms gaussian noise, each
 *             imp_db above the signal rms
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_common.h"
#include "chanutil.h"
#include "freedv_api.h"

#define FS 8000.0
#define HEADROOM_PEAK 4000.0

extern int mx_chanest, mx_noisew;
extern float mx_esnodb, mx_noisew_k;
int mx_blank = 0;

static bench_rng rng = {1357911131517ULL};
static double gauss(void) { return bench_rng_gauss(&rng); }
static double zero_noise(void *ctx) { (void)ctx; return 0.0; }

typedef struct { int c, n, b; float e; char name[32]; } variant_t;

/* Time-domain impulse blanker: zero samples whose magnitude exceeds k times a
 * running median-ish level (mean absolute value over a long window). */
static void blank(short *x, int n, double k) {
  double lvl = 0.0;
  int w = 2048;
  for (int i = 0; i < n && i < w; i++) lvl += fabs((double)x[i]);
  lvl /= (n < w ? n : w);
  for (int i = 0; i < n; i++) {
    double a = fabs((double)x[i]);
    if (a > k * lvl) x[i] = 0;
    else lvl += (a - lvl) / w;
  }
}

int main(int argc, char **argv) {
  if (argc < 7) {
    fprintf(stderr, "usage: rxlab MODE trials lo hi step chan [variants] [tone_hz tone_db] [imp_rate imp_db]\n");
    return 1;
  }
  int mode = bench_mode_from_name(argv[1]);
  int trials = atoi(argv[2]);
  double lo = atof(argv[3]), hi = atof(argv[4]), step = atof(argv[5]);
  int chan = chanutil_preset_from_name(argv[6]);
  const char *vlist = argc > 7 ? argv[7] : "c0";
  double tone_hz = argc > 9 ? atof(argv[8]) : 0.0;
  double tone_db = argc > 9 ? atof(argv[9]) : -99.0;
  double imp_rate = argc > 11 ? atof(argv[10]) : 0.0;
  double imp_db = argc > 11 ? atof(argv[11]) : 0.0;
  if (mode < 0 || chan < 0) { fprintf(stderr, "bad mode/channel\n"); return 1; }
  if (getenv("NOISEW_K")) mx_noisew_k = (float)atof(getenv("NOISEW_K"));

  variant_t v[16];
  int nv = 0;
  char tmp[256];
  strncpy(tmp, vlist, sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = 0;
  for (char *t = strtok(tmp, ","); t && nv < 16; t = strtok(NULL, ",")) {
    v[nv].c = 0; v[nv].n = 0; v[nv].b = 0; v[nv].e = -99.0f;
    strncpy(v[nv].name, t, 31); v[nv].name[31] = 0;
    for (char *q = t; *q;) {
      char k = *q++;
      double val = strtod(q, &q);
      if (k == 'c') v[nv].c = (int)val;
      else if (k == 'n') v[nv].n = (int)val;
      else if (k == 'b') v[nv].b = (int)val;
      else if (k == 's') v[nv].e = (float)val;
    }
    nv++;
  }

  struct freedv *tx = freedv_open(mode);
  struct freedv *rx[16];
  for (int k = 0; k < nv; k++) {
    rx[k] = freedv_open(mode);
    freedv_set_frames_per_burst(rx[k], 1);
  }
  freedv_set_frames_per_burst(tx, 1);

  int nbytes = freedv_get_bits_per_modem_frame(tx) / 8;
  int nburst = freedv_get_n_tx_preamble_modem_samples(tx) + freedv_get_n_tx_modem_samples(tx) +
               freedv_get_n_tx_postamble_modem_samples(tx);
  int lead = (int)(0.4 * FS), tail = nburst;
  int ntot = lead + nburst + tail;

  short *burst = malloc(sizeof(short) * (size_t)(nburst + 4));
  short *buf = malloc(sizeof(short) * (size_t)ntot);
  short *work = malloc(sizeof(short) * (size_t)ntot);
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
        mx_chanest = v[k].c; mx_noisew = v[k].n; mx_esnodb = v[k].e;
        const short *in = buf;
        if (v[k].b) {
          memcpy(work, buf, sizeof(short) * (size_t)ntot);
          blank(work, ntot, (double)v[k].b);
          in = work;
        }
        int ss = 0, got = 0;
        bench_run_burst(rx[k], in, ntot, 0, 0.0, out, zero_noise, NULL, &ss, &got, NULL, NULL);
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
  return 0;
}
