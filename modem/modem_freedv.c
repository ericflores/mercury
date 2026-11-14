/* FreeDV modem backend — codec2/FreeDV behind the modem_backend_t vtable.
 *
 * Copyright (C) 2026 Rhizomatica
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "modem_freedv.h"
#include "freedv_api.h"
#include "codec2_ofdm.h"
#include "../common/hermes_log.h"
#include <stdlib.h>
#include <string.h>

/* ctx is the struct freedv *. */
#define FDV(ctx) ((struct freedv *)(ctx))

/* The alternative channel estimate (docs/improvements/09): a second decode
 * attempt, from an estimate over more pilots, for packets whose stock decode
 * failed.  OFF unless asked for: audio:rx_chanest in the ini
 * (modem_freedv_set_chanest(), before the instances are opened) or
 * MERCURY_CHANEST=off|t4|t4f3|t4f5|auto.  "auto" is
 * time averaging alone on the modes of 3 or 4 carriers, where there is
 * nothing to smooth across, and 1-2-1 across carriers on the rest. */
#define CHANEST_AUTO (-1)
static int g_chanest = OFDM_CHANEST_OFF;

void modem_freedv_set_chanest(int mode) { g_chanest = mode; }

/* The environment variable, when set, wins over the ini value: it is the
 * A/B switch for a bench or an on-air session. */
static int chanest_setting(void)
{
    const char *e = getenv("MERCURY_CHANEST");
    if (!e || !*e)
        return g_chanest;
    return !strcmp(e, "auto") ? CHANEST_AUTO
         : !strcmp(e, "t4")   ? OFDM_CHANEST_T4
         : !strcmp(e, "t4f3") ? OFDM_CHANEST_T4F3
         : !strcmp(e, "t4f5") ? OFDM_CHANEST_T4F5
         : OFDM_CHANEST_OFF;
}

static int chanest_for_mode(int mode)
{
    int c = chanest_setting();
    if (c != CHANEST_AUTO)
        return c;
    switch (mode)
    {
    case FREEDV_MODE_DATAC15: case FREEDV_MODE_DATAC16:
    case FREEDV_MODE_DATAC13: case FREEDV_MODE_DATAC14:
    case FREEDV_MODE_DATAC4:
        return OFDM_CHANEST_T4;
    default:
        return OFDM_CHANEST_T4F3;
    }
}

/* Cyclic-prefix recycling (docs/improvements/19): one more decode attempt,
 * after the others, from rows whose prefix samples were averaged with their
 * twins.  The value is the guard in samples at the head of the prefix; 0 is
 * off.  OFF unless asked for: audio:rx_cp_recycle in the ini
 * (modem_freedv_set_cp_recycle(), before the instances are opened) or
 * MERCURY_CP_RECYCLE=<samples>, which wins when set. */
static int g_cp_recycle = 0;

void modem_freedv_set_cp_recycle(int guard_samples)
{
    g_cp_recycle = guard_samples;
}

static int cp_recycle_setting(void)
{
    const char *e = getenv("MERCURY_CP_RECYCLE");
    if (!e || !*e)
        return g_cp_recycle;
    return atoi(e);
}

/* Window placement from the pilots' delay profile (docs/improvements/26): one
 * more decode attempt, after the others, from the burst demodulated again
 * with the FFT window and the frame grid following the paths found in the
 * pilot rows.  Receiver only.  OFF unless asked for: audio:rx_profile_track
 * in the ini (modem_freedv_set_profile_track(), before the instances are
 * opened) or MERCURY_PROFILE_TRACK=1, which wins when set (any other value
 * switches it off).  Modes with fewer than 9 carriers ignore it. */
static int g_profile_track = 0;

void modem_freedv_set_profile_track(int on)
{
    g_profile_track = on ? 1 : 0;
}

static int profile_track_setting(void)
{
    const char *e = getenv("MERCURY_PROFILE_TRACK");
    if (!e || !*e)
        return g_profile_track;
    return e[0] == '1';
}

/* Noise-aware soft bits (docs/improvements/07): a per-carrier noise profile
 * from the pilots and, for a packet that failed while the profile singles out
 * carriers, one more decode attempt with those carriers scaled down.
 * Receiver only.  OFF unless asked for: audio:rx_noise_weight in the ini
 * (modem_freedv_set_noise_weight(), before the instances are opened) or
 * MERCURY_NOISE_WEIGHT=1, which wins when set (any other value switches it
 * off). */
static int g_noise_weight = 0;

void modem_freedv_set_noise_weight(int on)
{
    g_noise_weight = on ? 1 : 0;
}

static int noise_weight_setting(void)
{
    const char *e = getenv("MERCURY_NOISE_WEIGHT");
    if (!e || !*e)
        return g_noise_weight;
    return e[0] == '1';
}

/* QAM16C2 transmit clipping (docs/improvements/02): the top rung leaves the
 * transmitter with a peak-to-average ratio of 11.4 dB, 4.4 dB more than the
 * rung below, so a radio set by its peaks radiates that much less average
 * power on it.  The measured clip setting brings it to 6.7 dB.  Transmit
 * side only; any receiver decodes it.  OFF unless asked for:
 * audio:qam16c2_tx_clip in the ini (modem_freedv_set_qam16c2_clip(), before
 * the instances are opened) or MERCURY_QAM16C2_CLIP=1, which wins when set
 * (any other value switches it off). */
static int g_qam16c2_clip = 0;

void modem_freedv_set_qam16c2_clip(int on)
{
    g_qam16c2_clip = on ? 1 : 0;
}

static int qam16c2_clip_setting(void)
{
    const char *e = getenv("MERCURY_QAM16C2_CLIP");
    if (!e || !*e)
        return g_qam16c2_clip;
    return e[0] == '1';
}

/* Early termination of LDPC decodes that are going nowhere
 * (docs/improvements/23): a frame that fails spends 100 iterations finding
 * that out, and every optional decode attempt pays that again.  With this on
 * a decode whose count of satisfied parity checks has stalled below 80 % is
 * abandoned.  OFF unless asked for: audio:rx_early_stop in the ini
 * (modem_freedv_set_early_stop(), before the instances are opened) or
 * MERCURY_RX_EARLY_STOP=1, which wins when set (any other value switches it
 * off). */
static int g_early_stop = 0;

void modem_freedv_set_early_stop(int on)
{
    g_early_stop = on ? 1 : 0;
}

static int early_stop_setting(void)
{
    const char *e = getenv("MERCURY_RX_EARLY_STOP");
    if (!e || !*e)
        return g_early_stop;
    return e[0] == '1';
}

static void *fdv_open(int mode)
{
    /* FSK_LDPC needs an advanced open (was open_freedv_mode_locked in modem.c).
     * All OFDM data modes use the plain open. */
    if (mode == FREEDV_MODE_FSK_LDPC)
    {
        char codename[80] = "H_256_768_22";
        struct freedv_advanced adv = {0, 4, 50, 8000, 750, 250, codename};
        return freedv_open_advanced(mode, &adv);
    }
    struct freedv *f = freedv_open(mode);
    if (f)
    {
        freedv_set_chanest_alt(f, chanest_for_mode(mode));
        freedv_set_cp_recycle(f, cp_recycle_setting());
        freedv_set_profile_track(f, profile_track_setting());
        freedv_set_noise_weight(f, noise_weight_setting());
        freedv_set_ldpc_early_stop(f, early_stop_setting());
        if (mode == FREEDV_MODE_QAM16C2 && qam16c2_clip_setting())
            freedv_set_qam16c2_tx_clip(f, 1);
    }
    return f;
}

static void fdv_close(void *ctx)                 { if (ctx) freedv_close(FDV(ctx)); }

static void fdv_configure(void *ctx, int frames_per_burst, int verbosity)
{
    freedv_set_frames_per_burst(FDV(ctx), frames_per_burst);
    freedv_set_verbose(FDV(ctx), verbosity);
}

static int fdv_bits_per_frame(void *ctx)   { return freedv_get_bits_per_modem_frame(FDV(ctx)); }
static int fdv_n_tx_samples(void *ctx)     { return freedv_get_n_tx_modem_samples(FDV(ctx)); }
static int fdv_n_max_rx_samples(void *ctx) { return freedv_get_n_max_modem_samples(FDV(ctx)); }
static int fdv_n_nom_samples(void *ctx)    { return freedv_get_n_nom_modem_samples(FDV(ctx)); }
static int fdv_sample_rate(void *ctx)      { return freedv_get_modem_sample_rate(FDV(ctx)); }

static int fdv_bandwidth_hz(void *ctx)
{
    /* nc carriers one symbol rate apart; see freedv_get_modem_bandwidth_hz. */
    return (int)(freedv_get_modem_bandwidth_hz(FDV(ctx)) + 0.5f);
}
static int fdv_get_mode(void *ctx)         { return freedv_get_mode(FDV(ctx)); }
static int fdv_frames_per_burst(void *ctx) { return freedv_get_frames_per_burst(FDV(ctx)); }

static int fdv_preamble_tx(void *ctx, int16_t *out)
{
    return freedv_rawdatapreambletx(FDV(ctx), out);
}

static int fdv_rawdata_tx(void *ctx, int16_t *out, const uint8_t *frame)
{
    freedv_rawdatatx(FDV(ctx), out, (unsigned char *)frame);
    return freedv_get_n_tx_modem_samples(FDV(ctx));
}

static int fdv_postamble_tx(void *ctx, int16_t *out)
{
    return freedv_rawdatapostambletx(FDV(ctx), out);
}

static int fdv_nin(void *ctx) { return freedv_nin(FDV(ctx)); }

static int fdv_rawdata_rx(void *ctx, uint8_t *bytes_out, const int16_t *demod_in)
{
    int n = (int)freedv_rawdatarx(FDV(ctx), bytes_out, (short *)demod_in);
    /* A frame delivered by one of the optional decode attempts (2 = the
     * alternative channel estimate, 3 = the recycled cyclic prefix, 4 = the
     * profile-tracker replay, 5 = the noise-weighted attempt) says so in the timing log: it is the only way
     * to see, in a session, that a switch did something.  Paths 0 and 1 (the
     * stock decode and the HARQ combine) stay silent, so with the switches
     * off the log is what it was. */
    if (n > 0)
    {
        int path = freedv_get_rx_decode_path(FDV(ctx));
        if (path >= 2)
            HLOGT("modem-rx", "rx decode path %d (mode %d, %d bytes)", path,
                  freedv_get_mode(FDV(ctx)), n);
    }
    return n;
}

static void fdv_get_stats(void *ctx, int *sync, float *snr)
{
    freedv_get_modem_stats(FDV(ctx), sync, snr);
}

static int  fdv_get_rx_status(void *ctx)      { return freedv_get_rx_status(FDV(ctx)); }
static void fdv_harq_reset(void *ctx)         { freedv_harq_reset(FDV(ctx)); }
static void fdv_set_harq(void *ctx, int on)   { freedv_set_harq(FDV(ctx), on); }
static void fdv_set_crc_seed(void *ctx, uint16_t seed, int accept_plain)
{ freedv_set_crc_seed(FDV(ctx), seed, accept_plain); }
static int  fdv_rx_crc_seeded(void *ctx)      { return freedv_get_rx_crc_seeded(FDV(ctx)); }
static void fdv_unsync(void *ctx)             { freedv_set_sync(FDV(ctx), FREEDV_SYNC_UNSYNC); }
static int  fdv_get_freq_offset(void *ctx, float *hz) { return freedv_get_freq_offset(FDV(ctx), hz); }

const modem_backend_t modem_backend_freedv = {
    .name             = "freedv",
    .open             = fdv_open,
    .close            = fdv_close,
    .configure        = fdv_configure,
    .bits_per_frame   = fdv_bits_per_frame,
    .n_tx_samples     = fdv_n_tx_samples,
    .n_max_rx_samples = fdv_n_max_rx_samples,
    .n_nom_samples    = fdv_n_nom_samples,
    .sample_rate      = fdv_sample_rate,
    .bandwidth_hz     = fdv_bandwidth_hz,
    .get_mode         = fdv_get_mode,
    .frames_per_burst = fdv_frames_per_burst,
    .preamble_tx      = fdv_preamble_tx,
    .rawdata_tx       = fdv_rawdata_tx,
    .postamble_tx     = fdv_postamble_tx,
    .nin              = fdv_nin,
    .rawdata_rx       = fdv_rawdata_rx,
    .get_stats        = fdv_get_stats,
    .get_rx_status    = fdv_get_rx_status,
    .harq_reset       = fdv_harq_reset,
    .set_harq         = fdv_set_harq,
    .unsync           = fdv_unsync,
    .set_crc_seed     = fdv_set_crc_seed,
    .rx_crc_seeded    = fdv_rx_crc_seeded,
    .get_freq_offset  = fdv_get_freq_offset,
};
