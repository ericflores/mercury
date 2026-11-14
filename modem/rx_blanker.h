/* rx_blanker -- impulse-noise blanker for captured audio.
 *
 * Zeroes the samples of short, strong noise bursts (ignition, switching
 * supplies, electric fences, static clicks) before any decoder sees them.
 * A short burst that is tens of dB above the signal spreads over every
 * carrier of the OFDM symbols it lands in and takes whole frames with it;
 * the same samples set to zero cost a small fraction of two or three symbols,
 * which the code absorbs.  docs/improvements/08-impulse-blanker.md.
 *
 * No dependencies: unit-testable alone, and usable by the bench tools.
 *
 * Copyright (C) 2026 Rhizomatica
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef RX_BLANKER_H
#define RX_BLANKER_H

#include <stdbool.h>
#include <stdint.h>

/* Lookahead, in samples: the decision about a sample is taken with this many
 * samples after it in hand, and the audio comes out this much later.  8 ms at
 * 8 kHz.  See rx_blanker_process(). */
#define RX_BLANKER_LOOKAHEAD 64

typedef struct {
    float    k;          /* threshold, multiples of the tracked level; 0 = off */
    float    level;      /* leaky mean of |x| over samples that were passed    */
    int      win;        /* its time constant in samples (256 ms)              */
    int      hold_len;   /* samples blanked after each trigger (1 ms)          */
    int      fs;

    int16_t  ring[RX_BLANKER_LOOKAHEAD];
    int      head;       /* index of the oldest sample in ring                 */
    int      filled;     /* samples in ring, until it is full                  */

    int      init_n;     /* samples seen while the level was being initialised */
    double   init_sum;
    bool     level_ok;

    bool     in_run;     /* inside an impulse being blanked                    */
    int      run_len;    /* samples blanked in this run                        */
    int      hold;       /* samples still to blank after the last trigger      */

    uint64_t seen, blanked, impulses, adopted;
    uint32_t sec_seen, sec_blanked;   /* the per-second safety cap             */
    bool     suspended;  /* cap hit: audio passes until the next second        */
    uint64_t suspensions;
} rx_blanker_t;

/* k = 0 switches it off: rx_blanker_process() then leaves the buffer alone
 * (no delay either).  The recommended value when on is 6. */
void rx_blanker_init(rx_blanker_t *b, float k, int fs);

/* In place.  Returns the number of samples zeroed in this call.  When on, the
 * output is the input delayed by RX_BLANKER_LOOKAHEAD samples (the first
 * RX_BLANKER_LOOKAHEAD samples out are zeros). */
int rx_blanker_process(rx_blanker_t *b, int16_t *x, int n);

/* Fraction of samples blanked since init. */
double rx_blanker_fraction(const rx_blanker_t *b);

#endif
