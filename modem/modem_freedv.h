/* FreeDV modem backend — codec2/FreeDV behind the modem_backend_t vtable.
 *
 * A thin 1:1 adapter over the freedv_api: the instance context is the
 * struct freedv *.  This is backend #1; behaviour is identical to the former
 * direct freedv_* calls in modem.c.
 *
 * Copyright (C) 2026 Rhizomatica
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef MERCURY_MODEM_FREEDV_H
#define MERCURY_MODEM_FREEDV_H

#include "modem_backend.h"

extern const modem_backend_t modem_backend_freedv;

/* The alternative channel estimate for instances opened from now on:
 * OFDM_CHANEST_* (codec2_ofdm.h), or -1 for the per-mode choice.  Overrides
 * MERCURY_CHANEST.  Off by default. */
void modem_freedv_set_chanest(int mode);

/* Cyclic-prefix recycling for instances opened from now on: the guard in
 * samples at the head of the prefix, 0 = off (default).  Overridden by
 * MERCURY_CP_RECYCLE when that is set. */
void modem_freedv_set_cp_recycle(int guard_samples);

/* Window placement from the pilots' delay profile for instances opened from
 * now on: 0 = off (default), 1 = on.  Overridden by MERCURY_PROFILE_TRACK
 * when that is set. */
void modem_freedv_set_profile_track(int on);

#endif /* MERCURY_MODEM_FREEDV_H */
