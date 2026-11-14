/* rx_notch -- automatic notch for steady carriers.  See rx_notch.h.
 *
 * Detection, on the audio as captured (never on the notched output, where a
 * removed line could not be seen to still be there):
 *
 *  - a Hann-windowed 1024-point spectrum every 64 ms;
 *  - per bin, the MINIMUM power over the last second of spectra.  Noise and
 *    modulated signals fluctuate from spectrum to spectrum, so their minimum
 *    falls far below their mean; a steady line's does not.  A line that is on
 *    for part of the second (an MFSK tone, a burst's pilots) has a minimum of
 *    nearly nothing;
 *  - a bin is a line when its minimum is a local peak and stands NOTCH_ON_DB
 *    above the mean power of its neighbourhood (the 8 bins on each side
 *    beyond the window's main lobe).  A carrier 9 dB below an OFDM signal of
 *    1.7 kHz stands 14 dB above the signal's density in a 7.8 Hz bin, and
 *    below that level a carrier does no harm (docs/NOISE-WEIGHT-FINDINGS.md);
 *  - its frequency: the bin's phase advances by 2 pi f H / fs from one
 *    spectrum to the next.  The advance, averaged as a vector over the
 *    second, gives f to a small fraction of a hertz; the bin number resolves
 *    the fs/H ambiguity.
 *
 * Removal: a second-order notch with zeros on the unit circle at f and poles
 * at radius r behind them, NOTCH_BW_HZ wide at -3 dB.  Narrow, because inside
 * an OFDM signal the notch also takes that slice out of the carrier it falls
 * on; the frequency estimate is good enough for it.
 *
 * A notch follows its line while it moves less than a bin per second, and is
 * released when the line has been absent for NOTCH_RELEASE spectra.
 *
 * Copyright (C) 2026 Rhizomatica
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "rx_notch.h"

#include <math.h>
#include <string.h>

#define NOTCH_ON_DB    10.0f  /* to engage: minimum over the neighbourhood mean */
#define NOTCH_HOLD_DB   4.0f  /* to stay engaged                                */
#define NOTCH_BW_HZ     6.0   /* -3 dB width                                    */
#define NOTCH_RELEASE  32     /* spectra without the line: 2 s                  */
#define NOTCH_GUARD     3     /* bins each side that belong to the line itself  */
#define NOTCH_SPAN      8     /* bins each side, beyond the guard, for the mean */
#define NOTCH_FMIN_HZ 150.0f  /* nothing below or above the passband is touched */
#define NOTCH_FMAX_HZ 3600.0f

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* In-place radix-2 FFT, n a power of two. */
static void fft(float *re, float *im, int n)
{
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * M_PI / len;
        float wr = (float)cos(ang), wi = (float)sin(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (int k = 0; k < len / 2; k++) {
                int a = i + k, b = i + k + len / 2;
                float xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
                re[b] = re[a] - xr; im[b] = im[a] - xi;
                re[a] += xr; im[a] += xi;
                float t = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = t;
            }
        }
    }
}

void rx_notch_init(rx_notch_t *s, bool on, int fs)
{
    memset(s, 0, sizeof(*s));
    s->on = on;
    s->fs = fs > 0 ? fs : 8000;
}

int rx_notch_active(const rx_notch_t *s, float *f_hz, int max)
{
    int n = 0;
    for (int i = 0; i < RX_NOTCH_MAX; i++)
        if (s->n[i].active) {
            if (n < max && f_hz) f_hz[n] = s->n[i].f_hz;
            n++;
        }
    return n;
}

static void set_coefs(rx_notch_filter_t *f, double f_hz, int fs)
{
    double w = 2.0 * M_PI * f_hz / fs;
    double r = 1.0 - M_PI * NOTCH_BW_HZ / fs;
    /* unity gain away from the notch: normalise at DC and at fs/2 alike */
    double g = (1.0 + r * r) / 2.0;
    f->b0 = g;
    f->b1 = -2.0 * g * cos(w);
    f->b2 = g;
    f->a1 = -2.0 * r * cos(w);
    f->a2 = r * r;
    f->f_hz = (float)f_hz;
}

/* Minimum of bin k over the history. */
static float min_hold(const rx_notch_t *s, int k)
{
    float m = s->hist[0][k];
    for (int h = 1; h < RX_NOTCH_HIST; h++)
        if (s->hist[h][k] < m) m = s->hist[h][k];
    return m;
}

/* Mean power of the neighbourhood of bin k, over the history. */
static float neighbourhood(const rx_notch_t *s, int k)
{
    double acc = 0.0;
    int cnt = 0;
    for (int d = NOTCH_GUARD + 1; d <= NOTCH_GUARD + NOTCH_SPAN; d++)
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            int q = k + sgn * d;
            if (q < 1 || q >= RX_NOTCH_NBINS - 1) continue;
            for (int h = 0; h < RX_NOTCH_HIST; h++) acc += s->hist[h][q];
            cnt += RX_NOTCH_HIST;
        }
    return cnt ? (float)(acc / cnt) : 0.0f;
}

/* The line's frequency from the averaged phase advance of bin k. */
static double line_freq(const rx_notch_t *s, int k)
{
    double hop_hz = (double)s->fs / RX_NOTCH_HOP; /* the advance wraps every this many Hz */
    double frac = atan2((double)s->dph_im[k], (double)s->dph_re[k]) / (2.0 * M_PI);
    double centre = (double)k * s->fs / RX_NOTCH_NFFT;
    double f = frac * hop_hz;
    f += hop_hz * floor((centre - f) / hop_hz + 0.5);
    return f;
}

static void spectrum(rx_notch_t *s)
{
    static const double TWO_PI = 2.0 * M_PI;
    float re[RX_NOTCH_NFFT], im[RX_NOTCH_NFFT];
    /* frame is a ring whose oldest sample is at nsamp mod NFFT */
    int start = (int)(s->nsamp % RX_NOTCH_NFFT);
    for (int i = 0; i < RX_NOTCH_NFFT; i++) {
        float w = 0.5f - 0.5f * (float)cos(TWO_PI * i / RX_NOTCH_NFFT);
        re[i] = s->frame[(start + i) % RX_NOTCH_NFFT] * w;
        im[i] = 0.0f;
    }
    fft(re, im, RX_NOTCH_NFFT);
    float *p = s->hist[s->hpos];
    for (int k = 0; k < RX_NOTCH_NBINS; k++) {
        p[k] = re[k] * re[k] + im[k] * im[k];
        float ph = (float)atan2((double)im[k], (double)re[k]);
        if (s->hcount > 0) {
            float d = ph - s->ph[k];
            /* a leaky vector average over about a second */
            s->dph_re[k] += ((float)cos((double)d) - s->dph_re[k]) / (RX_NOTCH_HIST / 2);
            s->dph_im[k] += ((float)sin((double)d) - s->dph_im[k]) / (RX_NOTCH_HIST / 2);
        }
        s->ph[k] = ph;
    }
    s->hpos = (s->hpos + 1) % RX_NOTCH_HIST;
    if (s->hcount < RX_NOTCH_HIST + 1) s->hcount++;
    if (s->hcount <= RX_NOTCH_HIST) return; /* not a full second yet */

    int kmin = (int)(NOTCH_FMIN_HZ * RX_NOTCH_NFFT / s->fs);
    int kmax = (int)(NOTCH_FMAX_HZ * RX_NOTCH_NFFT / s->fs);
    if (kmin < NOTCH_GUARD + 1) kmin = NOTCH_GUARD + 1;
    if (kmax > RX_NOTCH_NBINS - NOTCH_GUARD - 2) kmax = RX_NOTCH_NBINS - NOTCH_GUARD - 2;
    float on_ratio = powf(10.0f, NOTCH_ON_DB / 10.0f);
    float hold_ratio = powf(10.0f, NOTCH_HOLD_DB / 10.0f);
    double bin_hz = (double)s->fs / RX_NOTCH_NFFT;

    /* the notches already engaged: is the line still there, and where */
    for (int i = 0; i < RX_NOTCH_MAX; i++) {
        rx_notch_filter_t *f = &s->n[i];
        if (!f->active) continue;
        int k0 = (int)(f->f_hz / bin_hz + 0.5), best = -1;
        float bm = 0.0f;
        for (int k = k0 - 1; k <= k0 + 1; k++) {
            if (k < kmin || k > kmax) continue;
            float m = min_hold(s, k);
            if (m > bm) { bm = m; best = k; }
        }
        if (best >= 0 && bm > hold_ratio * neighbourhood(s, best)) {
            double fn = line_freq(s, best);
            f->miss = 0;
            if (fabs(fn - f->f_hz) < bin_hz) set_coefs(f, fn, s->fs); /* follow it */
        } else if (++f->miss >= NOTCH_RELEASE) {
            f->active = false;
            s->released++;
        }
    }

    /* new lines */
    for (int k = kmin; k <= kmax; k++) {
        float m = min_hold(s, k);
        if (m <= min_hold(s, k - 1) || m < min_hold(s, k + 1)) continue;
        float nb = neighbourhood(s, k);
        if (!(m > on_ratio * nb) || nb <= 0.0f) continue;
        double f_hz = line_freq(s, k);
        if (fabs(f_hz - k * bin_hz) > bin_hz) continue; /* the phase and the peak disagree */
        bool have = false;
        for (int i = 0; i < RX_NOTCH_MAX; i++)
            if (s->n[i].active && fabs(s->n[i].f_hz - f_hz) < 3.0 * bin_hz) have = true;
        if (have) continue;
        for (int i = 0; i < RX_NOTCH_MAX; i++)
            if (!s->n[i].active) {
                memset(&s->n[i], 0, sizeof(s->n[i]));
                set_coefs(&s->n[i], f_hz, s->fs);
                s->n[i].active = true;
                s->engaged++;
                break;
            }
    }
}

int rx_notch_process(rx_notch_t *s, int16_t *x, int n)
{
    if (!s->on)
        return 0;
    for (int i = 0; i < n; i++) {
        float in = (float)x[i];
        /* the last NFFT samples, as a ring */
        s->frame[s->nsamp % RX_NOTCH_NFFT] = in;
        s->nsamp++;
        if (++s->fill >= RX_NOTCH_HOP && s->nsamp >= RX_NOTCH_NFFT) {
            spectrum(s);
            s->fill = 0;
        }
        /* With no notch engaged y stays the input sample, bit for bit. */
        double y = in;
        for (int q = 0; q < RX_NOTCH_MAX; q++) {
            rx_notch_filter_t *f = &s->n[q];
            if (!f->active) continue;
            double o = f->b0 * y + f->z1;
            f->z1 = f->b1 * y - f->a1 * o + f->z2;
            f->z2 = f->b2 * y - f->a2 * o;
            y = o;
        }
        if (y > 32767.0) y = 32767.0;
        if (y < -32768.0) y = -32768.0;
        x[i] = (int16_t)lrint(y);
    }
    return rx_notch_active(s, NULL, 0);
}
