/* chan_classify -- what is limiting the link, from one packet's statistics
 * (docs/improvements/10-channel-telemetry.md, "First classification").
 *
 * Reported only: nothing in the receiver or the ARQ reads it.  A pure
 * function of its input; no dependencies, so it is unit-testable alone and
 * the bench tools use the very code the modem logs with.
 *
 * Copyright (C) 2026 Rhizomatica
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef CHAN_CLASSIFY_H
#define CHAN_CLASSIFY_H

typedef enum {
    CHAN_CLEAN = 0,   /* nothing is limiting beyond the margin to the rung   */
    CHAN_IMPULSIVE,   /* impulses (needs the blanker's or the gap statistics) */
    CHAN_NARROWBAND,  /* a carrier or a narrow signal on a few carriers      */
    CHAN_ISI,         /* delay spread beyond what the prefix absorbs         */
    CHAN_DOPPLER,     /* the channel changes faster than the pilots follow   */
    CHAN_TXDIST,      /* a flat channel and a clean signal, yet errors:
                       * distortion at the peer's transmitter                */
    CHAN_NOISE,       /* white noise, close to the rung's threshold          */
    CHAN_N_CLASSES
} chan_class_t;

/* What the classification reads.  A field that is not available is marked
 * and its rules are skipped: nothing is invented.
 *   blank_frac < 0   the blanker's fraction is not available
 *   gap_kurt   < 0   the silent-gap kurtosis is not available
 *   gap_peaks  < 0   the silent-gap line count is not available
 *   have_spread = 0  dopp_hz, dly_ms, evm_resid_db were not formed
 *   thresh_db NaN    the rung's threshold is not known */
typedef struct {
    float snr_db;
    float thresh_db;
    float nv_peak_db;     /* noisiest carrier over the median carrier, dB */
    int have_spread;
    float evm_resid_db;   /* data-symbol error over the noise and the mode's own
                           * transmitter floor (freedv_rx_frame_stats.evm_resid_db) */
    float noise_evm_db;   /* the EVM the pilot noise alone predicts: -noise_evm_db
                           * is the symbol SNR without any distortion, where
                           * snr_db (the modem's) is spoilt by it too */
    float dly_ms;
    float dopp_hz;
    float cp_ms;          /* the mode's cyclic prefix */
    float blank_frac;
    float gap_kurt;
    int gap_peaks;
} chan_class_in_t;

/* The thresholds, from the proposal; starting points, calibrated in
 * docs/CHANSTATS-FINDINGS.md. */
#define CHAN_BLANK_FRAC      0.005f /* blanked fraction for "impulsive" */
#define CHAN_KURT            6.0f
#define CHAN_NV_PEAK_DB      6.0f
#define CHAN_EXCESS_DB       3.0f
#define CHAN_DOPP_HZ         2.0f
#define CHAN_FLAT_MS         1.0f /* the proposal says 0.5; DATAC1 reads 0.3 on AWGN and 0.7 clipped hard */
#define CHAN_TXDIST_SNR_DB   12.0f /* of the noise alone, see noise_evm_db */
#define CHAN_NOISE_MARGIN_DB 3.0f

chan_class_t chan_classify(const chan_class_in_t *in);
const char *chan_class_name(chan_class_t c);

#endif
