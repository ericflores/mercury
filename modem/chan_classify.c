/* chan_classify -- see chan_classify.h. */
#include "chan_classify.h"

#include <math.h>

static const char *const names[CHAN_N_CLASSES] = {
    "clean", "impulsive", "narrowband", "isi", "doppler", "txdist", "noise",
};

const char *chan_class_name(chan_class_t c)
{
    return (unsigned)c < CHAN_N_CLASSES ? names[c] : "?";
}

/* Order matters; the first rule that matches wins. */
chan_class_t chan_classify(const chan_class_in_t *in)
{
    /* 1. Impulses.  Both measures are of the blanker and the silent gaps,
     * which the modem does not have yet: skipped while they are unavailable
     * (a negative value). */
    if (in->blank_frac > CHAN_BLANK_FRAC || in->gap_kurt > CHAN_KURT) return CHAN_IMPULSIVE;

    /* 2. A carrier or narrow signal: the silent gap's lines (not available
     * yet) or one carrier's pilot residual far over the median's. */
    if (in->gap_peaks > 0 || in->nv_peak_db > CHAN_NV_PEAK_DB) return CHAN_NARROWBAND;

    /* 3 to 5: errors the noise does not explain (the data symbols' error
     * over what the pilot noise predicts), told apart by what the pilots
     * show. */
    if (in->have_spread && in->evm_resid_db > CHAN_EXCESS_DB) {
        if (in->dly_ms > 0.5f * in->cp_ms) return CHAN_ISI;
        if (in->dopp_hz > CHAN_DOPP_HZ) return CHAN_DOPPLER;
        /* The proposal says "SNR > 12 dB".  The modem's SNR estimate is
         * made from the very errors being explained: on a transmitter
         * clipped hard it falls from 25 dB to 10 dB, and the rule could never
         * fire (docs/CHANSTATS-FINDINGS.md).  The SNR the pilots' noise
         * implies is not spoilt by it. */
        if (in->dly_ms < CHAN_FLAT_MS && -in->noise_evm_db > CHAN_TXDIST_SNR_DB) return CHAN_TXDIST;
    }

    /* 6. Close to the rung's threshold: noise decides. */
    if (!isnan(in->thresh_db) && in->snr_db < in->thresh_db + CHAN_NOISE_MARGIN_DB) return CHAN_NOISE;

    /* 7. */
    return CHAN_CLEAN;
}
