/* rx_notch -- automatic notch for steady carriers in captured audio.
 *
 * A carrier inside the passband (a broadcast heterodyne, a birdie, someone
 * tuning) does more than cover the OFDM carriers under it: it captures the
 * demodulator's frequency tracker, which sums the pilots of all carriers, and
 * above about the signal's own level it stops acquisition altogether.  Soft
 * bit weights (docs/improvements/07) cannot repair either.  This module finds
 * lines that stay on one frequency for a second, measures the frequency to a
 * fraction of a hertz, and removes each with a narrow notch, before any
 * decoder sees the audio.
 *
 * Nothing Mercury transmits stays on one frequency that long except the ATU
 * tuning carrier (TUNE), which nothing needs to decode.
 *
 * No dependencies: unit-testable alone, and usable by the bench tools.
 *
 * Copyright (C) 2026 Rhizomatica
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef RX_NOTCH_H
#define RX_NOTCH_H

#include <stdbool.h>
#include <stdint.h>

#define RX_NOTCH_NFFT   1024  /* 128 ms at 8 kHz: 7.8 Hz bins                  */
#define RX_NOTCH_HOP    512   /* a spectrum every 64 ms                        */
#define RX_NOTCH_HIST   16    /* spectra a line must persist through: 1 s      */
#define RX_NOTCH_MAX    4     /* simultaneous notches                          */
#define RX_NOTCH_NBINS  (RX_NOTCH_NFFT / 2 + 1)

typedef struct {
    bool   active;
    float  f_hz;              /* the frequency being notched                   */
    int    miss;              /* consecutive spectra without the line          */
    double b0, b1, b2, a1, a2;
    double z1, z2;            /* filter state (transposed direct form II)      */
} rx_notch_filter_t;

typedef struct {
    bool   on;
    int    fs;

    float  frame[RX_NOTCH_NFFT];           /* the last NFFT input samples      */
    int    fill;                           /* samples since the last spectrum  */
    uint64_t nsamp;

    float  hist[RX_NOTCH_HIST][RX_NOTCH_NBINS];  /* per-spectrum power         */
    float  ph[RX_NOTCH_NBINS];             /* phase of the previous spectrum   */
    float  dph_re[RX_NOTCH_NBINS];         /* averaged phase advance per hop,  */
    float  dph_im[RX_NOTCH_NBINS];         /*   as a vector                    */
    int    hpos, hcount;

    rx_notch_filter_t n[RX_NOTCH_MAX];
    uint64_t engaged, released;            /* counters                         */
} rx_notch_t;

/* on = false: rx_notch_process() leaves the buffer alone. */
void rx_notch_init(rx_notch_t *s, bool on, int fs);

/* In place, no delay.  Returns the number of notches active after the call. */
int rx_notch_process(rx_notch_t *s, int16_t *x, int n);

/* The frequencies being notched; returns how many (at most max). */
int rx_notch_active(const rx_notch_t *s, float *f_hz, int max);

#endif
