/* rx_blanker -- impulse-noise blanker.  See rx_blanker.h.
 *
 * Copyright (C) 2026 Rhizomatica
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "rx_blanker.h"

#include <stdlib.h>
#include <string.h>

/* A sample triggers when |x| > k * level, level being the mean of |x| over
 * the recent samples that were passed.  What triggers is one of two things,
 * and the samples after it tell them apart:
 *
 *  - an impulse: the second half of the lookahead (4 to 8 ms later) is back
 *    at the old level.  The sample is zeroed, and so is every sample that
 *    triggers after it and the hold_len samples after each of those (the
 *    quiet samples inside a noise burst), until none triggers;
 *
 *  - the start of something that lasts: a burst arriving after silence, a
 *    static crash of tens of milliseconds.  The second half of the lookahead
 *    is still high.  Nothing is zeroed and the level jumps to the mean of the
 *    lookahead.  Without this a burst 20 dB over the noise that preceded it
 *    would be blanked wholesale at its start, because the level only follows
 *    slowly (docs/improvements/08, "Start-of-burst behaviour").
 *
 * "Still high" is judged on the lower quartile of |x| over that second half,
 * against a fifth of the threshold.  For noise-like audio the lower quartile
 * is 0.4 of the mean of |x|, so this asks for a mean of half the threshold: a
 * burst that triggers by its level, not by one peak, has a mean |x| of at
 * least the threshold divided by its own peak-to-mean ratio, and OFDM's is
 * under 5.
 *
 * A run that outlasts the lookahead was not an impulse after all: it ends and
 * the level is adopted from the lookahead.
 *
 * Safety cap: more than 5 % of a second's samples blanked suspends blanking
 * for the rest of that second.  A threshold set too low, or a signal the
 * level estimate has not caught up with, then costs a passed-through second
 * and not the audio. */
#define BLANK_CAP_PERCENT 5
#define SUSTAIN_FRACTION 0.2f

void rx_blanker_init(rx_blanker_t *b, float k, int fs)
{
    memset(b, 0, sizeof(*b));
    b->k = k > 0.0f ? k : 0.0f;
    b->fs = fs > 0 ? fs : 8000;
    b->win = b->fs * 256 / 1000;
    b->hold_len = b->fs / 1000;
    if (b->win < 1) b->win = 1;
    if (b->hold_len < 1) b->hold_len = 1;
}

double rx_blanker_fraction(const rx_blanker_t *b)
{
    return b->seen ? (double)b->blanked / (double)b->seen : 0.0;
}

/* Mean |x| of ring entries [from, to) counted from the oldest. */
static float ring_mean(const rx_blanker_t *b, int from, int to)
{
    long s = 0;
    for (int i = from; i < to; i++)
        s += labs((long)b->ring[(b->head + i) % RX_BLANKER_LOOKAHEAD]);
    return (float)s / (float)(to - from);
}

/* The lower quartile of |x| over ring entries [from, to).  A level that
 * lasts shows in it; a second impulse inside the window does not, as long as
 * it fills less than three quarters of it (the mean would be carried off by
 * it, and the level adopted from two impulses 6 ms apart then let the next
 * ones through for a quarter of a second). */
static float ring_quartile(const rx_blanker_t *b, int from, int to)
{
    int v[RX_BLANKER_LOOKAHEAD], n = to - from;
    for (int i = 0; i < n; i++) {
        int a = abs((int)b->ring[(b->head + from + i) % RX_BLANKER_LOOKAHEAD]);
        int j = i;
        while (j > 0 && v[j - 1] > a) { v[j] = v[j - 1]; j--; }
        v[j] = a;
    }
    return (float)v[n / 4];
}

static void adopt(rx_blanker_t *b)
{
    b->level = ring_mean(b, 0, RX_BLANKER_LOOKAHEAD);
    b->in_run = false;
    b->hold = 0;
    b->adopted++;
}

int rx_blanker_process(rx_blanker_t *b, int16_t *x, int n)
{
    if (b->k <= 0.0f)
        return 0;
    int zeroed = 0;
    const int L = RX_BLANKER_LOOKAHEAD;
    for (int i = 0; i < n; i++) {
        int16_t in = x[i];
        if (b->filled < L) {
            /* filling the delay line: nothing comes out yet */
            b->ring[(b->head + b->filled) % L] = in;
            b->filled++;
            x[i] = 0;
            continue;
        }
        /* the oldest sample leaves now; the ring is it and the L-1 after it */
        int16_t s = b->ring[b->head];
        float a = (float)abs((int)s);
        int16_t out = s;

        if (!b->level_ok) {
            /* the first `win` samples set the level; nothing is blanked */
            b->init_sum += a;
            if (++b->init_n >= b->win) {
                b->level = (float)(b->init_sum / b->init_n);
                b->level_ok = true;
            }
        } else if (b->suspended) {
            b->level += (a - b->level) / (float)b->win;
        } else {
            float thr = b->k * b->level;
            bool trig = a > thr;
            if (b->in_run) {
                if (trig) b->hold = b->hold_len;
                if (trig || b->hold > 0) {
                    out = 0;
                    if (!trig) b->hold--;
                    if (++b->run_len > L) adopt(b); /* it lasts: not an impulse */
                } else {
                    b->in_run = false;
                    b->level += (a - b->level) / (float)b->win;
                }
            } else if (trig) {
                if (ring_quartile(b, L / 2, L) > SUSTAIN_FRACTION * thr) {
                    adopt(b); /* the start of something that lasts */
                } else {
                    b->in_run = true;
                    b->run_len = 1;
                    b->hold = b->hold_len;
                    b->impulses++;
                    out = 0;
                }
            } else {
                b->level += (a - b->level) / (float)b->win;
            }
        }

        b->seen++;
        b->sec_seen++;
        if (out == 0 && s != 0) {
            zeroed++;
            b->blanked++;
            b->sec_blanked++;
            if (b->sec_blanked * 100u > (uint32_t)b->fs * BLANK_CAP_PERCENT) {
                b->suspended = true;
                b->in_run = false;
                b->hold = 0;
                b->suspensions++;
            }
        }
        if (b->sec_seen >= (uint32_t)b->fs) {
            b->sec_seen = 0;
            b->sec_blanked = 0;
            b->suspended = false;
        }

        b->ring[b->head] = in;
        b->head = (b->head + 1) % L;
        x[i] = out;
    }
    return zeroed;
}
