/*---------------------------------------------------------------------------*\

  FILE........: ofdm_internal.h
  AUTHORS.....: David Rowe & Steve Sampson
  DATE CREATED: June 2017

  OFDM Internal definitions.

\*---------------------------------------------------------------------------*/

/*
  Copyright (C) 2017 David Rowe

  All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License version 2.1, as
  published by the Free Software Foundation.  This program is
  distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
  License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef OFDM_INTERNAL_H
#define OFDM_INTERNAL_H

#include <complex.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "codec2_ofdm.h"
#include "filter.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define TAU (2.0f * M_PI)
#define ROT45 (M_PI / 4.0f)
#define MAX_UW_BITS 192

#define cmplx(value) (cosf(value) + sinf(value) * I)
#define cmplxconj(value) (cosf(value) + sinf(value) * -I)

/* modem state machine states */
typedef enum { search, trial, synced } State;

typedef enum {
  unsync,   /* force sync state machine to lose sync, and search for new sync */
  autosync, /* falls out of sync automatically */
  manualsync /* fall out of sync only under operator control */
} Sync;

/* phase estimator bandwidth options */

typedef enum {
  low_bw, /* can only track a narrow freq offset, but accurate         */
  high_bw /* can track wider freq offset, but less accurate at low SNR */
} PhaseEstBandwidth;

/*
 * User-defined configuration for OFDM modem.  Used to set up
 * constants at init time, e.g. for different bit rate modems.
 */

struct OFDM_CONFIG {
  float tx_centre; /* TX Centre Audio Frequency */
  float rx_centre; /* RX Centre Audio Frequency */
  float fs;        /* Sample Frequency */
  float rs;        /* Symbol Rate */
  float ts;        /* symbol duration */
  float tcp;       /* Cyclic Prefix duration */
  float timing_mx_thresh;

  int nc;      /* Number of carriers */
  int ns;      /* Number of Symbol frames */
  int np;      /* number of modem frames per packet */
  int bps;     /* Bits per Symbol */
  int txtbits; /* number of auxiliary data bits */
  int nuwbits; /* number of unique word bits */
  int bad_uw_errors;
  int ftwindowwidth;
  int edge_pilots;
  char *state_machine; /* name of sync state machine used for this mode */
  char *codename;      /* name of LDPC code used with this mode */
  uint8_t tx_uw[MAX_UW_BITS]; /* user defined unique word */
  int amp_est_mode;
  bool tx_bpf_en;      /* default tx (mod) hilbert clipper BPF enable */
  bool rx_bpf_en;      /* default rx (demod) input BPF enable */
  float *tx_bpf_proto; /* low pass prototype for complex BPF */
  int tx_bpf_proto_n;  /* number of taps in low pass prototype */
  bool foff_limiter;   /* tames freq offset updates in low SNR */
  float amp_scale;     /* used to scale Tx waveform to approx FREEDV_PEAK with
                          clipper off */
  float clip_gain1;    /* gain we apply to Tx signal before clipping to control
                          PAPR*/
  float clip_gain2;    /* gain we apply to Tx signal after clipping and BBF to
                          control peak level */
  bool clip_en;
  char mode[16]; /* OFDM mode in string form */
  char *data_mode;
  float fmin;
  float fmax;
  float EsNodB; /* EsNo est used for LDPC decoder */
};

/* Profile tracking (Mercury, docs/improvements/26): the heap state lives in
 * ofdm.c, allocated when the feature is switched on. */
struct OFDM_PT;
#define OFDM_PT_MAXTRK 8   /* tracked paths */
#define OFDM_PT_MAXPATHS 3 /* paths one frame's matching pursuit can report */

struct OFDM {
  struct OFDM_CONFIG config;

  char mode[16]; /* mode in string form */
  /*
   * See 700D Part 4 Acquisition blog post and ofdm_dev.m routines
   * for how this was set
   */
  float timing_mx_thresh;

  int nc;
  int ns;  /* NS-1 = data symbols between pilots  */
  int bps; /* Bits per symbol */
  int m;   /* duration of each symbol in samples */
  int ncp; /* duration of CP in samples */
  int np;  /* number of modem frames per packet. In some modes we want */
  /* the total packet of data to span multiple modem frames, e.g. HF data */
  /* and/or when the FEC codeword is larger than the one */
  /* modem frame.  In other modes (e.g. 700D/2020) Np=1, ie the modem frame */
  /* is the same length as the packet/FEC frame. */
  int ftwindowwidth;
  int bitsperframe;  /* total bits in all data symbols in modem frame */
  int bitsperpacket; /* total bits in all data symbols in a packet */
  int rowsperframe;
  int samplespersymbol;
  int samplesperframe;
  int nrxbufhistory; /* extra storage at start of rxbuf to allow us to step back
                        in time */
  int nrxbufmin; /* min number of samples we need in rxbuf to process a modem
                    frame */
  int rxbufst;   /* start of rxbuf window used for demod of current rx frame */
  int pre, post; /* pre-amble and post-amble detections */
  int max_samplesperframe;
  int nuwframes;
  int nrxbuf;
  int ntxtbits; /* reserve bits/frame for aux text information */
  int nuwbits;  /* number of unique word bits used to achieve packet frame sync
                 */
  int bad_uw_errors; /* threshold for UW detection check */
  int uw_fails; /* number of times we exceeded bad_uw_errors and dropped sync */
  int edge_pilots;     /* insert pilots at 1 and Nc+2, to support low bandwidth
                          phase est */
  char *data_mode;     /* "", "streaming", "burst"  */
  int packetsperburst; /* for OFDM data modes, how many packets before we reset
                          state machine */
  int amp_est_mode;    /* amplitude estimtor algorithm */
  int tx_bpf_proto_n;  /* number of taps in low pass prototype */
  float amp_scale;
  float clip_gain1;
  float clip_gain2;
  bool clip_en;

  float tx_centre; /* TX Center frequency */
  float rx_centre; /* RX Center frequency */
  float fs;        /* Sample rate */
  float ts;        /* Symbol cycle time */
  float rs;        /* Symbol rate */
  float tcp;       /* Cyclic prefix duration */
  float tpacket;   /* time for one packet in ms */
  float inv_m;     /* 1/m */
  float tx_nlower; /* TX lowest carrier freq */
  float rx_nlower; /* RX lowest carrier freq */
  float doc;       /* division of radian circle */

  float fmin;
  float fmax;
  float freq_search_step;  /* coarse grid step, Hz */
  float freq_search_fine;  /* fine window half-width, Hz; 0 = derive from step */

  // Pointers

  struct quisk_cfFilter *tx_bpf;
  struct quisk_cfFilter *rx_bpf;

  complex float *pilot_samples;
  complex float *rxbuf;
  complex float *pilots;
  complex float **rx_sym;
  complex float *rx_np;
  complex float *tx_uw_syms;
  COMP *tx_preamble;
  COMP *tx_postamble;

  float *rx_amp;
  float *aphase_est_pilot_log;

  /* Alternative channel estimate (Mercury, docs/improvements/09): a second
   * phase/amplitude reference per carrier averaged over more pilots than the
   * stock two.  Only rx_np_alt/rx_amp_alt are written from it; sync, the
   * unique word, hard decisions and the SNR estimate keep the stock one.
   * chanest_alt 0 (default) computes nothing. */
  int chanest_alt;
  complex float *rx_np_alt;
  float *rx_amp_alt;
  complex float chanest_slope;

  /* Cyclic-prefix recycling (Mercury, docs/improvements/19): 0 = off, else the
   * guard in samples at the head of the prefix.  When on, every row the
   * demodulator transforms is transformed a second time from a copy of its
   * samples in which each prefix sample is averaged with its twin one symbol
   * length away (rx_sym_cp, same shape as rx_sym), and the alternative
   * estimate is run on those rows into rx_np_cp/rx_amp_cp with its own
   * phase-slope accumulator.  Nothing the stock or the chanest_alt paths use
   * is written from it. */
  int cp_recycle;
  complex float **rx_sym_cp;
  complex float *rx_np_cp;
  float *rx_amp_cp;
  complex float chanest_slope_cp;

  /* Window placement from the pilots' delay profile (Mercury,
   * docs/improvements/26): 0 = off.  When on, the samples shifted into rxbuf
   * are counted and the state the demodulator starts a burst from is saved,
   * so that a burst whose decode attempts all failed can be demodulated a
   * second time (ofdm_profile_replay()) by a chain whose FFT window and frame
   * grid follow the paths found in the pilot rows instead of the stock
   * correlator.  The live demodulator is not changed by any of it.  The
   * fields below the switch are the tracker's state: live in the replay's
   * scratch copy of this struct, idle in the instance itself. */
  int profile_track;
  struct OFDM_PT *pt;
  int64_t rx_count;      /* samples shifted into rxbuf so far */
  int pt_replay;         /* this struct is a replay's scratch copy */
  int pt_first, pt_last; /* in a replay: the loop's answer to "first/last frame" */
  int pt_stock_timing;   /* test only: a replay with the stock timing tracker */
  int pt_w;              /* the placed window, same coordinates as sample_point */
  int pt_hold;           /* frames since a path was last seen */
  int pt_ntrk;
  float pt_trk[OFDM_PT_MAXTRK];  /* tracked path lags, relative to the window */
  float pt_trkw[OFDM_PT_MAXTRK]; /* their weights */
  int pt_trka[OFDM_PT_MAXTRK];   /* frames since each was seen */
  int pt_npaths;                 /* last detection: paths found... */
  int pt_lag[OFDM_PT_MAXPATHS];  /* ...and their lags */
  int pt_delta;                  /* last placement: shift chosen */

  uint8_t tx_uw[MAX_UW_BITS];
  int *uw_ind;
  int *uw_ind_sym;

  // State enums
  State sync_state;
  State last_sync_state;

  // Sync enums
  Sync sync_mode;

  // Phase enums
  PhaseEstBandwidth phase_est_bandwidth;

  int phase_est_bandwidth_mode;

  // Complex
  complex float foff_metric;

  // Float
  float foff_est_gain;
  bool foff_limiter;
  float foff_est_hz;
  float timing_mx;
  float coarse_foff_est_hz;
  float timing_norm;
  float mean_amp;
  float *tx_bpf_proto;

  // Integer
  int clock_offset_counter;
  int verbose;
  int sample_point;
  int timing_est;
  int timing_valid;
  int ct_est;
  int nin;
  int uw_errors;
  int sync_counter;
  int frame_count;  /* general purpose counter of modem frames */
  int packet_count; /* data mode: number of packets received so far */
  int modem_frame;  /* increments for every modem frame in packet */

  // Boolean
  bool sync_start;
  bool sync_end;
  bool timing_en;
  bool foff_est_en;
  bool phase_est_en;
  bool tx_bpf_en;
  bool rx_bpf_en;
  bool dpsk_en;
  bool postambledetectoren; /* allows us to optionally disable the postamble
                               detector */

  char *codename;
  float EsNodB; /* EsNo est used for LDPC decoder */
  char *state_machine;

  /* --- FFT-accelerated burst acquisition search (Mercury) ---
   * The coarse timing/frequency search is a cross-correlation of the receive
   * window against the frequency-shifted preamble, and doing it by brute force
   * dominated idle RX CPU (measured 90 % of it).  Done by FFT instead; these
   * are allocated lazily on first use and sized to acq_nfft.  NULL/0 means the
   * brute-force path is in use, which is always a valid fallback. */
  void *acq_fwd;              /* kiss_fft_cfg, forward                    */
  void *acq_inv;              /* kiss_fft_cfg, inverse                    */
  int   acq_nfft;             /* transform length, 0 = not initialised    */
  void *acq_buf;              /* one block: a, A, b, B, C, c (6 * nfft)   */
  bool  acq_fft_en;           /* false forces the brute-force search      */
};

/* Prototypes */

complex float qpsk_mod(int *);
complex float qam16_mod(int *);
void qpsk_demod(complex float, int *);
void qam16_demod(complex float, int *, float);
void ofdm_txframe(struct OFDM *, complex float *, complex float[]);
void ofdm_assemble_psk_modem_packet(struct OFDM *, uint8_t[], uint8_t[],
                                    uint8_t[]);
void ofdm_assemble_psk_modem_packet_symbols(struct OFDM *, complex float[],
                                            COMP[], uint8_t[]);
void ofdm_disassemble_psk_modem_packet(struct OFDM *, complex float rx_syms[],
                                       float rx_amps[], COMP[], float[],
                                       short[]);
void ofdm_disassemble_psk_modem_packet_with_text_amps(struct OFDM *,
                                                      complex float rx_syms[],
                                                      float rx_amps[], COMP[],
                                                      float[], short[], int *);
void ofdm_extract_uw(struct OFDM *ofdm, complex float rx_syms[],
                     float rx_amps[], uint8_t rx_uw[]);
void ofdm_rand(uint16_t[], int);
void ofdm_rand_seed(uint16_t r[], int n, uint64_t seed);
void ofdm_generate_payload_data_bits(uint8_t data_bits[], int n);
void ofdm_generate_preamble(struct OFDM *ofdm, COMP *tx_preamble, int seed);
int ofdm_get_phase_est_bandwidth_mode(struct OFDM *);
void ofdm_set_phase_est_bandwidth_mode(struct OFDM *, int);
void ofdm_clip(complex float tx[], float clip_thresh, int n);
void ofdm_hilbert_clipper(struct OFDM *ofdm, complex float *tx, size_t n);
float ofdm_esno_est_calc(complex float *rx_sym, int nsym);
float ofdm_esno_est_dd(int bps, complex float *rx_sym, float *rx_amp, int nsym);
float ofdm_snr_from_esno(struct OFDM *ofdm, float EsNodB);
void ofdm_get_demod_stats(struct OFDM *ofdm, struct MODEM_STATS *stats,
                          complex float *rx_syms, int Nsymsperpacket);

/* Profile tracking (docs/improvements/26-window-placement.md).
 *
 * ofdm_profile_paths(): matching pursuit on pilot rows seen through one FFT
 * window.  H holds nrows rows of nc values, H[r*nc + i] = row_r[i+1] *
 * conj(pilot[i+1]) (the carrier's value with the known symbol removed).  A
 * path whose useful part starts -L samples after the window start (L negative:
 * the window starts inside that path's prefix) rotates carrier i by
 * exp(+j*2*pi*(i+1)*L/m).  Fills lag[] (-m/2 .. m/2-1, circular) and pw[]
 * (energy explained per carrier) for up to OFDM_PT_MAXPATHS paths, strongest
 * first, and ratio with the rows' energy over what the paths leave.  Returns
 * the number of paths.  Needs the feature switched on (the lag table is
 * allocated with it).
 *
 * ofdm_profile_place(): feeds one frame's paths to the tracks (ofdm->pt_*),
 * chooses the shift of the window that leaves the least of another symbol
 * inside it, applies it to ofdm->pt_w and to the tracks' lags and returns it.
 * `first` starts the tracks afresh (a burst's first frame). */
int ofdm_profile_paths(struct OFDM *ofdm, const complex float *H, int nrows,
                       int *lag, float *pw, float *ratio);
int ofdm_profile_place(struct OFDM *ofdm, int npaths, const int *lag,
                       const float *pw, int first);
void ofdm_profile_reset(struct OFDM *ofdm, int window);
/* Demodulate the packet whose decode attempts have all just failed a second
 * time, window and frame grid following the tracked paths, from the same
 * samples.  Fills np*rowsperframe*nc symbols and amplitudes from the
 * four-pilot-row estimate and returns the chain's own mean amplitude.
 * Returns 0 when the first frame has left the buffer, the feature is off, or
 * the packet is not the burst's first. */
int ofdm_profile_replay(struct OFDM *ofdm, complex float *syms, float *amps,
                        float *mean_amp);
/* Test only: replay with the stock timing tracker instead of the new one. */
void ofdm_profile_test_stock_timing(struct OFDM *ofdm, int on);
/* Frames the last replay erased because their rows were past the buffer. */
int ofdm_profile_nerased(struct OFDM *ofdm);

#ifdef __cplusplus
}
#endif

#endif
