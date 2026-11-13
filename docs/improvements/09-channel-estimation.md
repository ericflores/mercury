# 09 — Channel estimation from more than two pilots (issue #9)

**Kind:** receiver only. No wire change. **Depends on:** 00-bench.
**Switch:** `MERCURY_CHANEST` / `[audio] rx_chanest`, default off.

## Goal

Recover the roughly 1 dB that the data modes lose to a noisy phase and
amplitude reference, without ever losing a frame that decodes today.

## Facts (verify the line numbers before editing; they drift)

- `modem/freedv/ofdm.c`, `ofdm_demod_core()` (about 1783). For each carrier
  `i = 1..nc` it forms `aphase_est_pilot[i]` and `aamp_est_pilot[i]`. In the
  `high_bw` branch (about 2082-2110) that is the mean of `rx_sym[1][i]`
  ("this" pilot) and `rx_sym[ns+1][i]` ("next" pilot), each multiplied by
  `conjf(ofdm->pilots[i])`.
- The data modes always run `high_bw`: only the voice state machine switches
  to `low_bw` (`ofdm.c:2315`); `ofdm_sync_state_machine_data_burst`
  (`ofdm.c:2401`) does not.
- All four pilot rows are already in `ofdm->rx_sym`: `[0]` previous, `[1]`
  this, `[ns+1]` next, `[ns+2]` future. Rows `[2..ns]` are the data symbols.
- The corrected symbols go to `ofdm->rx_np[(rr*nc) + (i-1)]` and their
  amplitudes to `ofdm->rx_amp[...]` (same index). `rr` is the data row, `i`
  the carrier, so **index mod nc is the carrier**.
- `modem/freedv/freedv_700.c`, `freedv_comp_short_rx_ofdm()`: each modem
  frame's `rx_np`/`rx_amp` is appended to `f->rx_syms` / `f->rx_amps`
  (allocated at `freedv_700.c:239-241`, shifted at 577-584). When
  `ofdm->modem_frame == np - 1` the packet is complete: disassemble,
  de-interleave, `symbols_to_llrs`, `ldpc_decode_frame`, CRC (595-640).
- With `edge_pilots == 0` (all data modes) `pilots[0]` and `pilots[nc+1]` are
  zero; only carriers `1..nc` have pilots.
- Pitfalls 1, 2, 3 and 4 of the handbook all apply here.

## Prototype result

See issue #9 and `prototype/results/`. Summary: DATAC1 AWGN 0 dB 0/100 →
95-98/100; DATAC1 ccir-poor +1.9 dB 17 → 47-53; QAM16C2 AWGN +7 dB 0/50 →
50/50; DATAC15 AWGN −12 dB 4/200 → 28-38; DATAC17 AWGN +3 dB 26/60 → 58/60.
No single estimator was best everywhere under fading.

## Design: a second, parallel estimate used only as a fallback

Do **not** replace the stock estimate. Compute an alternative estimate
alongside it and decode with it only when the stock decode fails its CRC.
The stock path then stays bit-identical (sync, unique word, SNR estimate,
hard decisions), which is what makes this safe to ship behind a switch and
later to enable by default.

### In `struct OFDM` (`modem/freedv/ofdm_internal.h`)

```c
int            chanest_alt;       /* 0 = off (stock only); else an OFDM_CHANEST_* */
complex float *rx_np_alt;         /* rowsperframe * nc, like rx_np  */
float         *rx_amp_alt;        /* rowsperframe * nc, like rx_amp */
complex float  chanest_slope;     /* leaky sum for the phase slope across carriers */
```

Allocate and free next to `rx_np` / `rx_amp` (`ofdm.c:369-372`, `653-654`).

```c
enum { OFDM_CHANEST_OFF = 0,
       OFDM_CHANEST_T4,      /* time 1,3,3,1; no smoothing across carriers */
       OFDM_CHANEST_T4F3,    /* time 1,3,3,1; 1-2-1 across carriers        */
       OFDM_CHANEST_T4F5 };  /* time 1,3,3,1; 1-2-3-2-1 across carriers    */
```

### In `ofdm_demod_core()`, after the stock per-carrier loop

A new `static void ofdm_chanest_alt(struct OFDM *ofdm, float *aphase, float *aamp)`:

1. `h_prev[i], h_this[i], h_next[i], h_fut[i] = rx_sym[row][i] * conjf(pilots[i])`
   for `i = 1..nc`.
2. `first = (sync_state == trial && sync_counter == 0)`;
   `last = (sync_state == synced && modem_frame == np - 1)`.
   Time weights `(wp, wt, wn, wf) = (1, 3, 3, 1)`; set `wp = 0` if `first`,
   `wf = 0` if `last`. `g[i]` = weighted mean.
3. Phase slope: `sl = Σ_{i=1}^{nc-1} g[i+1] * conjf(g[i])`;
   `chanest_slope = 0.7 * chanest_slope + sl` (reset to 0 on `first`);
   `th = cargf(chanest_slope)`.
4. Across carriers (F3/F5 only): for offsets `d` in the kernel, skipping
   carriers outside `1..nc`,
   `h += w[d] * g[i+d] * cmplxconj(th * d)`, then divide by the sum of the
   weights used.
5. `aphase[i] = cargf(h)`, `aamp[i] = cabsf(h)`.

Then fill `rx_np_alt` / `rx_amp_alt` in the same loop that fills `rx_np` /
`rx_amp`: `rx_np_alt[k] = rx_sym[rr+2][i] * cmplxconj(aphase_alt[i])`,
`rx_amp_alt[k] = aamp_alt[i]`. Nothing else reads the alternative estimate:
not `rx_bits`, not the unique word, not `mean_amp`, not the SNR estimate.

The prototype diff (`prototype/rxlab-prototype.diff`) has this code in
working form; lift the arithmetic, not the globals.

### In `freedv_700.c`

- `f->rx_syms_alt`, `f->rx_amps_alt`: same size and same shifting as
  `rx_syms` / `rx_amps`, filled from `ofdm->rx_np_alt` / `rx_amp_alt`.
- A second mean amplitude for scaling: keep a leaky mean of `rx_amp_alt` the
  way `ofdm->mean_amp` is kept, or reuse `ofdm->mean_amp` (the two are close;
  decide by measurement).
- After the stock decode and the existing HARQ fallback, if the CRC still
  failed and `ofdm->chanest_alt != 0`: run disassemble → de-interleave →
  `symbols_to_llrs` → `ldpc_decode_frame` → CRC on the `_alt` buffers. On
  success, copy the bits out exactly as the stock path does and set
  `FREEDV_RX_BITS`.
- Factor the "symbols to decoded bits" sequence into one static function
  taking `(syms, amps, EsNo, mean_amp)` so the stock, HARQ and alternative
  attempts share it. Proposals 07 and 05 add more attempts to the same list.
- HARQ: retain the **stock** `llr_raw` as today. Do not mix attempts into the
  retained sum.

### API (`freedv_api.{c,h}`, next to `freedv_set_timing_mx_thresh`)

```c
void freedv_set_chanest_alt(struct freedv *f, int mode);   /* OFDM_CHANEST_* */
int  freedv_get_chanest_alt(struct freedv *f);
int  freedv_get_rx_decode_path(struct freedv *f);   /* 0 stock, 1 harq, 2 alt: which attempt delivered the last frame */
```

### Backend and switch

- `modem_backend_t`: optional `void (*set_chanest_alt)(void *ctx, int mode)`;
  implement in `modem_freedv.c`; MFSK leaves it NULL.
- `modem.c`: a process-wide `modem_set_rx_chanest(int mode)` (atomic), applied
  to every pooled and worker-owned FreeDV instance when it is opened or bound
  (`pool_open_mode_locked`, `rx_decoder_bind_mode`, the `own_codec` paths).
- `MERCURY_CHANEST=0|t4|t4f3|t4f5|auto` and ini `[audio] rx_chanest`.
  `auto` = `T4` for modes with fewer than 9 carriers (DATAC15, DATAC16,
  DATAC13, DATAC4), `T4F3` otherwise; revise from the full matrix.
- Log, per session, how many frames each decode path delivered.

## Steps (each a reviewable commit)

1. `rx_bench` exists (00-bench).
2. Fields, allocation, `ofdm_chanest_alt()`, alt buffers; setter/getter.
   Switch off: unit test proves decoded bytes **and** `rx_status` sequence
   are identical to trunk on seeded noisy bursts for every OFDM mode.
3. Fallback attempt in `freedv_700.c`; `freedv_get_rx_decode_path`.
4. Backend hook, `modem.c` plumbing, env and ini switch, session counters.
5. Full bench matrix; choose `auto`; findings document
   `docs/CHANNEL-ESTIMATION-FINDINGS.md`.
6. Re-check `EsNodB` for the alternative path (it may want its own value:
   sweep 0, 1.5, 3, 4.5 dB at each mode's cliff with `freedv_set_esnodb`, a
   bench-only setter to add).
7. `tests/integration` A/B on AWGN and `ccir-poor` near a rung boundary.

## Tests

`tests/modem/test_ofdm_chanest.c` (Unity, linked against `libfreedvdata.a`
like `test_ofdm_acq`):

- `test_off_is_identical`: for each mode, a seeded burst at 3 SNRs decodes to
  the same bytes and the same count of sync/CRC events with the field at 0 as
  a build without the feature (pin the expected counts as constants).
- `test_alt_rescues_at_cliff`: DATAC1, AWGN 0 dB, 40 seeded bursts: stock
  alone delivers fewer than 5; with `T4F3` at least 30.
- `test_slope_is_handled`: DATAC1 at +5 dB and at +15 dB delivers 20/20 with
  `T4F3` (this is the case the unsloped smoother failed).
- `test_first_last_frames`: a burst preceded and followed by silence (no
  noise) decodes with the alternative path forced as the only path (a test
  hook), proving the first/last-frame weights are right.

## Bench matrix for the findings document

Modes: DATAC16, DATAC15, DATAC4, DATAC3, DATAC1, DATAC17, QAM16C2.
Channels: AWGN, `ccir-poor`, `ccir-moderate`, `itur-mdv`, `ccir-flutter`.
Variants: stock, T4, T4F3, T4F5, and stock+fallback (what ships).
Also one row per mode with ±20 Hz carrier offset (`--offset`), and the
noise-only control.

## Acceptance

- Switch off: identical to trunk (test above).
- Switch on: on every cell, delivered ≥ stock (it is a superset by
  construction; a shortfall means a bug or a false CRC).
- AWGN 90 % point at least 0.8 dB lower for DATAC1, DATAC17 and QAM16C2;
  DATAC15/16 reported honestly (they are acquisition-limited below about
  −11 dB, so the gain shows as delivered/acquired).
- False accepts in the noise-only control: zero in 1000 windows per mode.
- CPU: `utils/rxcost_bench` shows the per-frame cost when frames decode
  first time rises by under 5 %; the cost of a failed frame (one more LDPC
  run) is reported.

## Open questions to settle by measurement

- Whether `ARQ_SNR_MIN_*` should move once rungs work about 1 dB earlier.
  Do not change them in this proposal; note the finding.
- Whether to make the alternative the primary path later (saves the second
  LDPC run). Only after on-air evidence.
- Decision-directed refinement (use decoded or soft data symbols as extra
  pilots, then re-estimate) is a follow-up; it would be a third attempt in
  the same list.
- Side observation to raise with maintainers, not to fix here:
  `ofdm.c:2112` overwrites the `amp_est_mode` amplitude.
- Measured later (proposal 26, prototype section): giving each data row its
  own estimate by interpolating between pilot rows (linear, Catmull-Rom, or
  the four-pilot weights with a time slope) changes nothing on CCIR Poor or
  NVIS at 1 Hz Doppler. And on NVIS the limit is the FFT window and the
  timing tracker, not the estimate: see `26-window-placement.md`.
