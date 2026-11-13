# 02 — QAM16C2 transmit clipping behind a switch (issue #2)

**Kind:** transmitter only. No wire change. **Depends on:** nothing.
**Switch:** `MERCURY_QAM16C2_CLIP=1` / `[audio] qam16c2_tx_clip = true`,
default off.

## Goal

Let operators and the OTA bench turn on the clipping setting that
`docs/CLIPPED-QAM16C2-FINDINGS.md` already measured (PAPR 11.4 → 6.7 dB,
about 4 dB of radio setting on a peak-limited transmitter).

**Before starting:** that document says an over-the-air check was being
arranged. Ask the maintainers whether a branch already exists.

## Facts

- `modem/freedv/ofdm_mode.c:104-135`: `qam16c2` has `clip_en = false`
  (line 122), `amp_scale = 135E3`, `EsNodB = 10`.
- `freedv_set_tx_clip_config(f, clip_en, gain1, gain2, amp_scale)`
  (`freedv_api.c:1769`, declared `freedv_api.h:301`) changes the transmit
  waveform only. Its one caller today is `utils/qam16_papr_sweep.c:200`,
  which passes the mode's own `amp_scale` (135E3 for QAM16C2).
- Recommended setting from the findings: `gain1 = 2.0`, `gain2 = 1.0`. Drive
  2.5 loses under fading; 3.0 destroys the mode.
- Instances: `modem/modem.c` keeps one pooled instance per mode
  (`pool_open_mode_locked`, line 656; `pooled_codec_for_mode_locked`). The
  carousel's keydown builder (about line 1596-1680) takes the pooled
  instance of each frame's mode for transmit. The clip configuration only
  affects the transmit side of an instance, so setting it on the pooled
  QAM16C2 instance is enough.

## Design

1. `modem_backend_t`: optional
   `void (*set_tx_clip)(void *ctx, int enable, float gain1, float gain2);`
   In `modem_freedv.c`: call `freedv_set_tx_clip_config(FDV(ctx), enable,
   gain1, gain2, amp_scale)` with `amp_scale` 135E3 for QAM16C2. Prefer a
   getter for the instance's current `amp_scale` over a second copy of the
   constant (add `freedv_get_tx_amp_scale()` if none exists).
2. `modem.c`: `static _Atomic bool g_qam16c2_clip;`
   `void modem_set_qam16c2_clip(bool on);` In `pool_open_mode_locked()`,
   after `be->configure(...)`, if the mode is `FREEDV_MODE_QAM16C2` and the
   flag is set, call the hook with `(1, 2.0f, 1.0f)`. If the flag can change
   at runtime (ini reload from the UI), re-apply under the instance lock
   (`modem_inst_lock_for`).
3. Ini key and environment variable as in the handbook. The two gains are
   **not** exposed: the findings show how narrow the good region is.
4. Log one line at start-up when the switch is on.

## Steps

1. Hook, plumbing, switch.
2. Unit test.
3. Re-run the findings' own commands through a build with the switch (the
   sweep tool sets the config itself, so add a `--as-shipped` mode or a tiny
   test that reads the PAPR of a burst built by `modem.c`'s path).
4. Append an "As shipped" section to `docs/CLIPPED-QAM16C2-FINDINGS.md` with
   the measured PAPR of the switch path.
5. OTA A/B (maintainers / operators): same radios, switch flipped between
   sessions, compare the rung usage reported by `utils/onair_logs.py`.

## Tests (`tests/modem/test_tx_clip.c`, or extend `test_tx_bpf.c`)

- `test_default_unchanged`: a QAM16C2 burst generated with the switch off is
  sample-identical to one from a plain `freedv_open` (pin a checksum).
- `test_clip_lowers_papr`: with the switch on, PAPR of the burst (preamble,
  packet, postamble) is between 6.0 and 7.5 dB; off, above 10.5 dB.
- `test_clipped_decodes`: a clipped burst, no noise, decodes with a stock
  receiver instance (interoperability).
- `test_other_modes_untouched`: DATAC17's burst checksum is the same with the
  switch on.

## Acceptance

- Off: identical samples.
- On: PAPR 6.67 ± 0.3 dB (the findings' figure); `utils/qam16_papr_sweep
  qam16c2 100 3 11 awgn 2.0 1.0 1` knee unchanged from the findings.

## Follow-up, not in this change

- `ARQ_SNR_MIN_QAM16C2_DB` (13 dB, `arq_protocol.h:324`) was set from the
  unclipped goodput crossover. With clipping on at both ends the crossover
  against DATAC17 moves about 3 dB earlier under fading. Changing the
  threshold is a ladder change: it needs the sim matrix and belongs to a
  separate proposal once OTA confirms the clip.
- A clipped 16QAM constellation reads differently to the SNR estimator; the
  findings saw no extra bias (−4.5 ± 0.1 raw). Re-check
  `freedv_snr_calib()` (`freedv_700.c:497`) on the as-shipped path.
