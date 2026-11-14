/* FreeDV modem backend — codec2/FreeDV behind the modem_backend_t vtable.
 *
 * Copyright (C) 2026 Rhizomatica
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "modem_freedv.h"
#include "freedv_api.h"
#include "codec2_ofdm.h"
#include "../common/hermes_log.h"
#include "../datalink_arq/arq_protocol.h" /* the rungs' decode thresholds, read only */
#include "chan_classify.h"
#include <math.h>
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

/* One switch for a trial: every receiver-only improvement on at its
 * recommended setting (audio:rx_improvements in the ini,
 * modem_freedv_set_rx_all(); MERCURY_RX_ALL=1 wins when set).  Each feature
 * keeps its own switch: its environment variable, when set, still decides
 * for that feature alone, on or off, and its ini key can switch it on
 * without this.  Transmit-side switches are not part of it. */
static int g_rx_all = 0;

void modem_freedv_set_rx_all(int on) { g_rx_all = on ? 1 : 0; }

int modem_freedv_rx_all(void)
{
    const char *e = getenv("MERCURY_RX_ALL");
    if (e && *e)
        return e[0] == '1';
    return g_rx_all;
}

void modem_freedv_set_chanest(int mode) { g_chanest = mode; }

/* The environment variable, when set, wins over the ini value: it is the
 * A/B switch for a bench or an on-air session. */
static int chanest_setting(void)
{
    const char *e = getenv("MERCURY_CHANEST");
    if (!e || !*e)
        return g_chanest != OFDM_CHANEST_OFF ? g_chanest
             : modem_freedv_rx_all() ? CHANEST_AUTO : OFDM_CHANEST_OFF;
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
        return g_cp_recycle ? g_cp_recycle : modem_freedv_rx_all() ? 4 : 0;
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
        return g_profile_track || modem_freedv_rx_all();
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
        return g_noise_weight || modem_freedv_rx_all();
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
        return g_early_stop || modem_freedv_rx_all();
    return e[0] == '1';
}

/* Decision-directed channel estimation (docs/improvements/20): the last
 * decode attempt, from a channel estimate that uses the decoder's own belief
 * about the data symbols as well as the pilots.  Receiver only.  OFF unless
 * asked for: audio:rx_decision_directed in the ini
 * (modem_freedv_set_decision_directed(), before the instances are opened) or
 * MERCURY_DD=1, which wins when set (any other value switches it off). */
static int g_decision_directed = 0;

void modem_freedv_set_decision_directed(int on)
{
    g_decision_directed = on ? 1 : 0;
}

static int decision_directed_setting(void)
{
    const char *e = getenv("MERCURY_DD");
    if (!e || !*e)
        return g_decision_directed || modem_freedv_rx_all();
    return e[0] == '1';
}

/* Channel telemetry (docs/improvements/10, first part): one "chanstats:"
 * line in the timing log for every OFDM packet the receiver finishes,
 * decoded or not.  It changes nothing in what is decoded.  OFF unless asked
 * for: main:chanstats in the ini (modem_freedv_set_chanstats(), before the
 * instances are opened) or MERCURY_CHANSTATS=1, which wins when set. */
static int g_chanstats = 0;

void modem_freedv_set_chanstats(int on)
{
    g_chanstats = on ? 1 : 0;
}

/* The setting as the instances were opened with it: the receive path reads
 * this on every call and must not go to the environment each time. */
static int g_chanstats_on = 0;

/* The decode threshold of the rung a mode is (datalink_arq/arq_protocol.h);
 * NAN for a mode that is no rung, which switches the "noise" class off. */
static float chanstats_rung_thresh(int mode)
{
    switch (mode)
    {
    case FREEDV_MODE_DATAC4:  return ARQ_SNR_MIN_DATAC4_DB;
    case FREEDV_MODE_DATAC3:  return ARQ_SNR_MIN_DATAC3_DB;
    case FREEDV_MODE_DATAC1:  return ARQ_SNR_MIN_DATAC1_DB;
    case FREEDV_MODE_DATAC17: return ARQ_SNR_MIN_DATAC17_DB;
    case FREEDV_MODE_QAM16C2: return ARQ_SNR_MIN_QAM16C2_DB;
    default:                  return NAN;
    }
}

static int chanstats_setting(void)
{
    const char *e = getenv("MERCURY_CHANSTATS");
    if (e && *e)
        return e[0] == '1';
    return g_chanstats;
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
        freedv_set_decision_directed(f, decision_directed_setting());
        g_chanstats_on = chanstats_setting();
        freedv_set_noise_stats(f, g_chanstats_on);
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
     * profile-tracker replay, 5 = the noise-weighted attempt, 6 = the
     * decision-directed attempt) says so in the timing log: it is the only way
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
    /* Telemetry: one line per packet finished, decoded or not.  key=value,
     * fixed prefix, so a script can gather a session (utils/onair_logs.py).
     * nvpk is the noisiest carrier against the median carrier in dB and nvhz
     * its audio frequency: a carrier or a narrow signal on the frequency
     * shows there. */
    if (g_chanstats_on)
    {
        int st = freedv_get_rx_status(FDV(ctx));
        struct freedv_rx_frame_stats fs;
        if ((st & (FREEDV_RX_BITS | FREEDV_RX_BIT_ERRORS)) && freedv_get_rx_frame_stats(FDV(ctx), &fs))
        {
            /* dopp (Hz) and dly (ms) are coarse: a 3 s burst of 0.11 s pilot
             * rows cannot resolve a Doppler spread under about 0.5 Hz or
             * over 5 Hz, and dly reads the two-path separation that has the
             * same decorrelation across carriers.  evm is the data symbols'
             * error in dB under their power, evmx over what the pilot noise
             * predicts, evmr over that and the mode's own transmitter floor.
             * A field that could not be formed reads 0.  limit= is the first
             * classification (modem/chan_classify.c), reported only. */
            chan_class_in_t ci = {.snr_db = fs.snr_db,
                                  .thresh_db = chanstats_rung_thresh(freedv_get_mode(FDV(ctx))),
                                  .nv_peak_db = fs.noise_peak_db,
                                  .have_spread = (fs.chan_valid & 7) == 7,
                                  .evm_resid_db = fs.evm_resid_db,
                                  .dly_ms = fs.dly_ms,
                                  .dopp_hz = fs.dopp_hz,
                                  .cp_ms = fs.cp_ms,
                                  /* not built yet: the blanker's fraction and the silent-gap statistics */
                                  .blank_frac = -1.0f,
                                  .gap_kurt = -1.0f,
                                  .gap_peaks = -1};
            HLOGT("modem-rx",
                  "chanstats: mode=%d ok=%d path=%d snr=%.1f foff=%.1f pcc=%d/%d it=%d nvpk=%.1f nvhz=%.0f nvflag=%d"
                  " dopp=%.1f dly=%.2f evm=%.1f evmx=%.1f evmr=%.1f limit=%s",
                  freedv_get_mode(FDV(ctx)), fs.ok, fs.path, (double)fs.snr_db, (double)fs.foff_hz, fs.pcc, fs.npc,
                  fs.iters, (double)fs.noise_peak_db, (double)fs.noise_peak_hz, fs.noise_flagged, (double)fs.dopp_hz,
                  (double)fs.dly_ms, (double)fs.evm_db, (double)fs.evm_excess_db, (double)fs.evm_resid_db,
                  chan_class_name(chan_classify(&ci)));
        }
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
