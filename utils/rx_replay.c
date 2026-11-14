/* rx_replay -- decode a recording with several receiver variants.
 *
 * The recording is what a station's decoders were handed: raw s16le, mono,
 * 8 kHz, as MERCURY_RX_DUMP=<file> writes it (modem/modem.c, rx_dump_chunk),
 * or any capture converted to that format.  Every variant decodes the SAME
 * samples, so a frame one variant delivers and another does not is the
 * variant and nothing else: the paired comparison of rx_bench, on audio from
 * a real session (two stations over the integration harness, or the air)
 * instead of one synthetic burst.
 *
 *   rx_replay FILE [variants] [modes] [seed]
 *
 *   variants  comma list, default "stock,auto+cp4+pt".  Each is a set of
 *             exact tokens joined with '+':
 *               stock        the plain receiver
 *               auto         the alternative channel estimate as mercury's
 *                            MERCURY_CHANEST=auto picks it: t4 on the modes of
 *                            3 or 4 carriers, t4f3 on the rest (proposal 09)
 *               t4 t4f3 t4f5 that estimate on every mode
 *               cpN          cyclic-prefix recycling, guard N (proposal 19)
 *               pt           the profile-tracker replay (proposal 26); modes
 *                            of fewer than 9 carriers ignore it, as in mercury
 *               nw           noise-aware soft bits (proposal 07)
 *               bK           the impulse blanker at threshold K over the
 *                            audio before the decoder (proposal 08); the
 *                            recording is raw, as MERCURY_RX_DUMP writes it
 *                            before the blanker
 *             The first variant is the reference the others are compared to.
 *   modes     comma list of mode names, or "arq" (default): DATAC16, DATAC15,
 *             DATAC4, DATAC3, DATAC1, DATAC17, QAM16C2, the control mode and
 *             the OFDM rungs of the carousel.  The MFSK floor is not an OFDM
 *             mode and is not decoded here.
 *   seed      the session's CRC seed in hex (mercury logs it at TIMING level:
 *             "session CRC seed 0x...."), or 0 (default).  A session's
 *             payload frames carry the seed in their CRC and do not pass
 *             without it; frames with a plain CRC (CALL, ACCEPT, the
 *             session's own control frames) are accepted either way.
 *
 * Every decoder runs in burst mode with one frame per burst and no HARQ
 * combining, like mercury's monitor: within a session the carousel's frames
 * are different codewords, never copies.  A decoder listens to the whole
 * file whether or not its mode was on the air, as the monitor does; in a
 * session only the bound decoder runs, so false syncs counted here for modes
 * that were never sent cost nothing there.
 *
 * Output, per mode and variant: frames delivered (CRC ok), how many by each
 * decode path (0 stock, 2 alternative estimate, 3 recycled prefix, 4 profile
 * replay, 5 noise-weighted), and against the reference: gained (delivered here, not there) and
 * lost (delivered there, not here).  Frames are matched by content and by
 * position within one second.  "lost" must be 0 for a variant that only adds
 * attempts after the stock decode.
 *
 * Copyright (C) 2026 Rhizomatica
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_common.h"
#include "freedv_api.h"
#include "codec2_ofdm.h"
#include "rx_blanker.h"

#define FS 8000
#define MAX_VARIANTS 8
#define MAX_MODES 12
#define MATCH_SAMPLES FS /* same frame: same bytes, within one second */

#define EST_AUTO (-1)

typedef struct {
  int chanest; /* OFDM_CHANEST_* or EST_AUTO */
  int cp;
  int pt;
  int nw;
  float blank;
  char name[64];
} variant_t;

typedef struct {
  long pos;      /* sample index at which the frame was delivered */
  uint32_t hash; /* of its bytes */
  int path;
  int seeded;
} frame_t;

typedef struct {
  frame_t *f;
  int n, cap;
  int syncs;
} result_t;

static int parse_variant(const char *s, variant_t *v) {
  char tmp[64];
  int have_est = 0;
  if (strlen(s) >= sizeof(tmp) || !*s) return -1;
  strcpy(tmp, s);
  strcpy(v->name, s);
  v->chanest = OFDM_CHANEST_OFF;
  v->cp = 0;
  v->pt = 0;
  v->nw = 0;
  v->blank = 0.0f;
  char *save = NULL;
  for (char *t = strtok_r(tmp, "+", &save); t; t = strtok_r(NULL, "+", &save)) {
    int est = -2;
    if (!strcmp(t, "stock")) continue;
    if (!strcmp(t, "pt")) { v->pt = 1; continue; }
    if (!strcmp(t, "nw")) { v->nw = 1; continue; }
    if (t[0] == 'b' && t[1] >= '0' && t[1] <= '9') {
      char *end;
      double kb = strtod(t + 1, &end);
      if (*end || kb < 1.0 || kb > 100.0) { fprintf(stderr, "variant '%s': bad threshold in '%s'\n", s, t); return -1; }
      v->blank = (float)kb;
      continue;
    }
    if (!strcmp(t, "auto")) est = EST_AUTO;
    else if (!strcmp(t, "t4")) est = OFDM_CHANEST_T4;
    else if (!strcmp(t, "t4f3")) est = OFDM_CHANEST_T4F3;
    else if (!strcmp(t, "t4f5")) est = OFDM_CHANEST_T4F5;
    if (est != -2) {
      if (have_est) { fprintf(stderr, "variant '%s': two estimator tokens\n", s); return -1; }
      have_est = 1;
      v->chanest = est;
      continue;
    }
    if (t[0] == 'c' && t[1] == 'p' && t[2] >= '0' && t[2] <= '9') {
      char *end;
      long n = strtol(t + 2, &end, 10);
      if (*end || n < 1 || n > 1000) { fprintf(stderr, "variant '%s': bad guard in '%s'\n", s, t); return -1; }
      v->cp = (int)n;
      continue;
    }
    fprintf(stderr, "unknown variant token '%s' in '%s': stock, auto, t4, t4f3, t4f5, cpN, pt, nw, bK\n", t, s);
    return -1;
  }
  return 0;
}

/* The estimate MERCURY_CHANEST=auto gives a mode (modem/modem_freedv.c,
 * chanest_for_mode): time averaging alone where there are 3 or 4 carriers. */
static int auto_chanest(int mode) {
  switch (mode) {
    case FREEDV_MODE_DATAC15:
    case FREEDV_MODE_DATAC16:
    case FREEDV_MODE_DATAC13:
    case FREEDV_MODE_DATAC14:
    case FREEDV_MODE_DATAC4:
      return OFDM_CHANEST_T4;
    default:
      return OFDM_CHANEST_T4F3;
  }
}

static uint32_t fnv1a(const unsigned char *p, int n) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
  return h;
}

static void add_frame(result_t *r, long pos, uint32_t hash, int path, int seeded) {
  if (r->n == r->cap) {
    r->cap = r->cap ? 2 * r->cap : 256;
    r->f = realloc(r->f, sizeof(frame_t) * (size_t)r->cap);
    if (!r->f) { fprintf(stderr, "out of memory\n"); exit(1); }
  }
  r->f[r->n++] = (frame_t){pos, hash, path, seeded};
}

/* Frames of a that have no partner in b. */
static int count_missing(const result_t *a, const result_t *b) {
  int miss = 0;
  for (int i = 0; i < a->n; i++) {
    int found = 0;
    for (int j = 0; j < b->n && !found; j++) {
      long d = a->f[i].pos - b->f[j].pos;
      if (d < 0) d = -d;
      found = a->f[i].hash == b->f[j].hash && d <= MATCH_SAMPLES;
    }
    miss += !found;
  }
  return miss;
}

static void decode_file(int mode, const variant_t *v, uint16_t seed, const short *s, long n, result_t *r) {
  struct freedv *f = freedv_open(mode);
  if (!f) { fprintf(stderr, "cannot open mode %d\n", mode); exit(1); }
  freedv_set_frames_per_burst(f, 1);
  freedv_set_harq(f, 0);
  freedv_set_crc_seed(f, seed, 1);
  freedv_set_chanest_alt(f, v->chanest == EST_AUTO ? auto_chanest(mode) : v->chanest);
  freedv_set_cp_recycle(f, v->cp);
  freedv_set_profile_track(f, v->pt); /* refused by modes of fewer than 9 carriers */
  freedv_set_noise_weight(f, v->nw);

  int nbytes = freedv_get_bits_per_modem_frame(f) / 8;
  unsigned char *out = malloc((size_t)nbytes + 8);
  long pos = 0;
  int was_sync = 0;
  memset(r, 0, sizeof(*r));
  for (;;) {
    int nin = freedv_nin(f);
    if (pos + nin > n) break;
    int nb = (int)freedv_rawdatarx(f, out, (short *)&s[pos]);
    pos += nin;
    int st = freedv_get_rx_status(f);
    int sync = (st & FREEDV_RX_SYNC) != 0;
    if (sync && !was_sync) r->syncs++;
    was_sync = sync;
    if (nb > 0 && !(st & FREEDV_RX_BIT_ERRORS))
      add_frame(r, pos, fnv1a(out, nb), freedv_get_rx_decode_path(f), freedv_get_rx_crc_seeded(f));
  }
  free(out);
  freedv_close(f);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: rx_replay FILE [variants] [modes] [seed]\n"
                    "  FILE      raw s16le mono 8 kHz (MERCURY_RX_DUMP)\n"
                    "  variants  default stock,auto+cp4+pt\n"
                    "  modes     default arq (DATAC16,DATAC15,DATAC4,DATAC3,DATAC1,DATAC17,QAM16C2)\n"
                    "  seed      session CRC seed, hex; default 0\n");
    return 1;
  }
  const char *vlist = argc > 2 ? argv[2] : "stock,auto+cp4+pt";
  const char *mlist = argc > 3 ? argv[3] : "arq";
  uint16_t seed = argc > 4 ? (uint16_t)strtoul(argv[4], NULL, 16) : 0;
  if (!strcmp(mlist, "arq")) mlist = "DATAC16,DATAC15,DATAC4,DATAC3,DATAC1,DATAC17,QAM16C2";

  variant_t v[MAX_VARIANTS];
  int nv = 0;
  char tmp[256];
  strncpy(tmp, vlist, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = 0;
  for (char *t = strtok(tmp, ","); t; t = strtok(NULL, ",")) {
    if (nv == MAX_VARIANTS) { fprintf(stderr, "at most %d variants\n", MAX_VARIANTS); return 1; }
    if (parse_variant(t, &v[nv])) return 1;
    nv++;
  }
  int modes[MAX_MODES], nm = 0;
  char mname[MAX_MODES][16];
  strncpy(tmp, mlist, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = 0;
  for (char *t = strtok(tmp, ","); t; t = strtok(NULL, ",")) {
    if (nm == MAX_MODES) { fprintf(stderr, "at most %d modes\n", MAX_MODES); return 1; }
    int m = bench_mode_from_name(t);
    if (m < 0) { fprintf(stderr, "unknown mode '%s'\n", t); return 1; }
    modes[nm] = m;
    strncpy(mname[nm], t, sizeof(mname[nm]) - 1);
    mname[nm][sizeof(mname[nm]) - 1] = 0;
    nm++;
  }

  FILE *fp = fopen(argv[1], "rb");
  if (!fp) { perror(argv[1]); return 1; }
  fseek(fp, 0, SEEK_END);
  long n = ftell(fp) / (long)sizeof(short);
  fseek(fp, 0, SEEK_SET);
  short *s = malloc(sizeof(short) * (size_t)(n > 0 ? n : 1));
  if (!s || fread(s, sizeof(short), (size_t)n, fp) != (size_t)n) {
    fprintf(stderr, "%s: cannot read %ld samples\n", argv[1], n);
    return 1;
  }
  fclose(fp);

  printf("%s: %ld samples, %.1f s; seed 0x%04x; reference variant: %s\n", argv[1], n, (double)n / FS, seed,
         v[0].name);
  /* the audio each variant decodes: the recording, or a blanked copy of it */
  short *sv[MAX_VARIANTS];
  for (int k = 0; k < nv; k++) {
    sv[k] = s;
    if (v[k].blank > 0.0f) {
      rx_blanker_t bl;
      sv[k] = malloc(sizeof(short) * (size_t)(n > 0 ? n : 1));
      if (!sv[k]) { fprintf(stderr, "out of memory\n"); return 1; }
      memcpy(sv[k], s, sizeof(short) * (size_t)n);
      rx_blanker_init(&bl, v[k].blank, FS);
      rx_blanker_process(&bl, sv[k], (int)n);
      printf("variant %s: %.4f %% of the samples blanked, %llu impulses\n", v[k].name,
             100.0 * rx_blanker_fraction(&bl), (unsigned long long)bl.impulses);
    }
  }

  printf("%-8s %-18s %6s %7s  %5s %5s %5s %5s %5s  %6s %5s  %6s\n", "mode", "variant", "frames", "seeded", "p0",
         "p2", "p3", "p4", "p5", "gained", "lost", "syncs");

  int tot[MAX_VARIANTS] = {0}, totg[MAX_VARIANTS] = {0}, totl[MAX_VARIANTS] = {0};
  for (int m = 0; m < nm; m++) {
    result_t r[MAX_VARIANTS];
    for (int k = 0; k < nv; k++) decode_file(modes[m], &v[k], seed, sv[k], n, &r[k]);
    for (int k = 0; k < nv; k++) {
      int p[6] = {0}, seeded = 0;
      for (int i = 0; i < r[k].n; i++) {
        int path = r[k].f[i].path;
        if (path >= 0 && path <= 5) p[path]++;
        seeded += r[k].f[i].seeded;
      }
      int gained = k ? count_missing(&r[k], &r[0]) : 0;
      int lost = k ? count_missing(&r[0], &r[k]) : 0;
      tot[k] += r[k].n;
      totg[k] += gained;
      totl[k] += lost;
      printf("%-8s %-18s %6d %7d  %5d %5d %5d %5d %5d  %6d %5d  %6d\n", mname[m], v[k].name, r[k].n, seeded, p[0],
             p[2], p[3], p[4], p[5], gained, lost, r[k].syncs);
    }
    for (int k = 0; k < nv; k++) free(r[k].f);
    fflush(stdout);
  }
  for (int k = 0; k < nv; k++)
    printf("%-8s %-18s %6d %7s  %5s %5s %5s %5s %5s  %6d %5d\n", "all", v[k].name, tot[k], "", "", "", "", "", "",
           totg[k], totl[k]);
  for (int k = 0; k < nv; k++)
    if (sv[k] != s) free(sv[k]);
  free(s);
  return 0;
}
