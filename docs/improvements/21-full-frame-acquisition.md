# 21 — Acquire on the whole frame's pilots (issue #21)

**Kind:** receiver only. No wire change.
**Depends on:** 00-bench; complements 03 (schedule-aided acquisition) and 23
(time budget).
**Switch:** `MERCURY_FRAME_ACQ` / `[audio] rx_frame_acq`, default off.

## Goal

Lower the acquisition threshold of the OFDM modes by detecting a burst on
all the known signal it contains, so that acquisition stops being the limit
of the robust rungs and of the control frame.

## Facts

- Acquisition: `ofdm_sync_search_burst()` (`modem/freedv/ofdm.c:1577`) →
  `burst_acquisition_detector()` → `est_timing_and_freq()`: a normalised
  correlation of the received samples against the stored preamble (and
  postamble), over a coarse then a fine time and frequency grid. Sync is
  declared when `timing_mx > timing_mx_thresh`. The preamble and postamble
  are one modem frame each (`ofdm_generate_preamble`, seeds 2 and 3).
- After the declaration, the unique word must match within `bad_uw_errors`
  (`ofdm_sync_state_machine_data_burst`, `ofdm.c:2401`).
- Known signal in a burst: preamble (5 symbol rows), postamble (5), and one
  pilot row per modem frame: `np` rows (28 for DATAC16, 34 DATAC15, 29
  DATAC3, 38 DATAC1, 61 DATAC17, 31 QAM16C2), plus the unique-word symbols.
- Measured acquisition against decoding (this investigation, white noise,
  per 150): DATAC16 at −13 dB: 40 acquired, 22 of them decoded with 09 + 19;
  at −14 dB: 25 acquired, 6 decoded. DATAC15 at −12 dB: 75 acquired, 27
  decoded. QAM16C2 under CCIR Poor acquired 36-44 of 50 between +9.5 and
  +13.6 dB.
- `docs/ACQ-TUNING-FINDINGS.md`: a lower threshold buys about 2 dB and is
  limited by false syncs (2-6 % of windows at 0.35, collapse at 0.30).
- For bursts acquired on the postamble the receiver already rewinds its
  buffer by one packet (`ofdm->rxbufst -= np * samplesperframe`), so one
  burst of history is kept (`nrxbufhistory`).

## Design: two stages

### Stage 1: nominate

Today's detector with a lower threshold (start at the stock value minus
0.10, floor 0.25). Every time-frequency peak above it is a *candidate*,
not a sync. Keep the best `C` candidates per search window (`C` is the
effort knob: 2 on a Pi, 16 on a desktop, hundreds with a GPU).

### Stage 2: confirm on the pilots of the frame

A candidate gives the packet's start `t0` and frequency `f0`. The pilot row
of modem frame `p` then starts at `t0 + p · samplesperframe`.

For each `p` in `0..np` and carrier `i` in `1..nc`:

```
c[p][i] = DFT_i( samples of that row, de-rotated by f0 ) · conj(pilot[i])
```

First metric to evaluate, differential across consecutive pilot rows:

```
D = | Σ_p Σ_i  c[p][i] · conj(c[p−1][i]) |  /  Σ_p Σ_i |c[p][i]|²
```

Why this form: a residual frequency error is a constant phase step from
one pilot row to the next, and a timing error is a fixed phase per carrier;
both cancel in each product's phase up to a common term, so the sum over
all `np · nc` products stays coherent without a fine frequency or timing
search. Its cost is the squaring loss of multiplying two noisy terms, which
grows as the SNR per symbol falls. It also needs the channel to hold for
one modem frame (0.11 s), not for the burst.

Alternatives to compare offline: coherent sum over the whole burst for each
of a fine grid of frequencies (best on a static channel, needs steps of a
fraction of 1/burst-length, about 0.1 Hz); coherent over blocks of 2-4 rows
and non-coherent across blocks.

Declare sync when `D` exceeds its own threshold, set from a noise-only run.
Then proceed exactly as today from the trial state: the unique word and the
CRC still follow.

### Buffering

Stage 2 needs the whole burst, so it runs when stage 1's candidate is one
burst length old. Extend the rewind that postamble acquisition uses: keep
`np + 2` modem frames of history for the bound mode and, on confirmation,
set `rxbufst` to the packet's first frame and replay. Decoding then
finishes later than today by up to one burst length **only for bursts that
today are not acquired at all**; bursts the stock detector finds follow the
stock path with no delay. Check the consequence for the decode allowance
(`D_OFDM_MS`) with proposal 23: a burst confirmed late may miss its slot,
which is still better than never decoding it, but the carousel must treat
it as a late frame, not as a timing anchor.

## Steps

1. **Offline study, no modem change.** With `rx_bench`, dump the samples of
   bursts at and below each mode's acquisition cliff, and noise-only
   windows. In a standalone tool compute the stock metric and the stage-2
   candidates' metrics at the true `t0`, `f0` and at wrong ones. Plot or
   tabulate detection against false alarm. This decides the metric and the
   threshold, and says how many dB there really are.
2. Stage 1 candidate list and stage 2 in `ofdm.c`; history buffer; switch.
3. `utils/acq_tuning_sweep` and `utils/acquire_vs_decode` with the feature:
   acquired and delivered per SNR, the noise-only control over the same
   windows, a ±20 Hz and ±50 Hz offset.
4. Fading presets (`ccir-poor`, `ccir-flutter`): the differential metric's
   one-frame coherence assumption is tested there.
5. With proposal 03: inside an armed window the candidates are restricted
   in time and frequency, which lowers the false-alarm rate further.
6. Timing (proposal 23), carousel handling of late frames, findings.

## Tests

- Unarmed and off: status sequence identical to trunk.
- A burst 3 dB below the stock acquisition cliff is found in most of 40
  seeds; noise-only windows produce no confirmed sync in 500 seeds at the
  chosen thresholds.
- A burst of a *different* mode in the passband is not confirmed (the pilot
  pattern and carrier set differ).
- Late confirmation replays the right samples: the decoded frame equals the
  sent one.

## Acceptance

- Off: identical to trunk.
- Acquisition 50 % point at least 2 dB lower than stock for DATAC16 and
  DATAC15 on white noise, with a noise-only confirmed-sync rate no higher
  than stock's.
- Delivered frames at −13 dB (DATAC16, with 09 and 19 on) at least doubled.
- No loss of delivered frames at any SNR on any preset.
