# 03 — Schedule-aided ("directed") acquisition (issue #3)

**Kind:** receiver only. No wire change. **Depends on:** 00-bench.
**Switch:** `MERCURY_DIRECTED_ACQ=1` / `[audio] rx_directed_acq`, default off.

## Goal

Acquire bursts that are lost today although their payload would decode, by
using what the receiver already knows: the next burst of a round starts at a
known sample offset and at the previous burst's frequency offset.

## Facts

- A round is one keydown. `modem/modem.c` (about 1648-1672) builds it as:
  head silence, then for each frame `i`: (if `i > 0`) exactly
  `8000 * gap_ms / 1000` zero samples, then `append_burst_audio()` =
  preamble + packet + postamble. So **the start of preamble `i+1` is exactly
  postamble length + gap samples after the end of packet `i`**, in the
  transmitted audio. `gap_ms` is 100, or 200 for QAM16C2 and MFSK
  (`burst_gap_ms`, `carousel.c:192`).
- The receiver's payload decoder is bound to the round's mode by
  `bind_rx(c, lv)` (`carousel.c:690`) through the `io.bind_rx(ctx, mode)`
  callback (`carousel.h:79`). The receiver also knows how many frames it
  asked for (`poll_n`) and each frame says how many follow.
- Acquisition: `ofdm_sync_search_burst()` (`ofdm.c:1577`) runs
  `burst_acquisition_detector()` (`ofdm.c:1547`) on the preamble and, if
  enabled, the postamble. The detector calls `est_timing_and_freq()` over a
  window of `2 * samplesperframe` samples, a coarse grid `ofdm->fmin ..
  ofdm->fmax` in steps of `freq_search_step`, then a fine pass. Sync is
  declared when `timing_mx > ofdm->timing_mx_thresh`.
- After a packet, the burst state machine returns to `search` and zeroes
  `rxbuf` (`ofdm.c:2446-2453`).
- `docs/ACQ-TUNING-FINDINGS.md`: threshold 0.35 instead of 0.40/0.45 gives
  about +2 dB of acquisition for DATAC16/15 but 2-6 % false syncs per search
  window; 0.30 collapses (30-40 % false syncs). The capture range is ±55 Hz
  (`docs/OFFSET-ACQUISITION-FINDINGS.md`).
- QAM16C2 under `ccir-poor` acquired only 36-44 of 50 bursts between +9.5
  and +13.6 dB in the prototype runs (`prototype/results/Q2_qam_poor.txt`),
  matching `docs/CLIPPED-QAM16C2-FINDINGS.md`.

## Design

Two stages; the first is self-contained in the decoder.

### Stage B first: bursts 2..N of a round (decoder-internal, sample-exact)

In `struct OFDM`:

```c
int   armed_gap;        /* samples from the end of a packet's postamble to the next preamble; 0 = not armed */
int   armed_left;       /* bursts still expected in this round */
int   armed_countdown;  /* samples until the expected preamble start; <0 = no expectation */
int   armed_window;     /* half-width in samples (default 160 = 20 ms) */
float armed_thresh;     /* threshold used inside the window (default: stock - 0.10, floor 0.30) */
float armed_foff;       /* frequency prior: the previous burst's tracked foff_est_hz */
float armed_foff_win;   /* half-width in Hz (default 3.0) */
```

- `ofdm_arm_round(ofdm, gap_samples, n_more)` sets `armed_gap`, `armed_left`.
- When a packet completes (state machine `synced` → `search`), if
  `armed_left > 0`: `armed_countdown = postamble_samples + armed_gap`
  (measured from the end of the packet's last modem frame; derive the exact
  constant from the modem-frame and postamble lengths and **verify it by
  logging `ct_est` of the next burst in a clean train**), `armed_foff =
  foff_est_hz`, `armed_left--`.
- `armed_countdown` decreases by the samples consumed on each call.
- In `ofdm_sync_search_burst()`: if the search window `[st, st +
  2*samplesperframe)` overlaps `[expected − window, expected + window]`:
  - search frequency only in `armed_foff ± armed_foff_win` (pass these as
    `fmin`/`fmax` to the detector instead of `ofdm->fmin/fmax`);
  - accept a timing estimate only if it falls inside the window;
  - use `armed_thresh`.
  Outside the window the stock search runs unchanged, so an unexpected burst
  is still found.
- The unique-word check (`bad_uw_errors`) and the CRC still gate everything.
- Disarm on mode change, on `freedv_set_sync(UNSYNC)`, and when
  `armed_countdown` passes `−window` without a sync (the burst was not sent
  or was lost; fall back to the stock search for the rest of the round).

API: `freedv_arm_round(f, gap_ms, n_more)`; backend hook `arm_round`;
`modem.c` passes it to the payload worker's instance.

Carousel side: extend the io callback rather than adding a second one:
`bind_rx(ctx, mode)` stays, and a new optional `expect_round(ctx, mode,
gap_ms, n)` is called where the receiver sends its poll (`send_poll_ex`,
`carousel.c:1856`) with `gap_ms = burst_gap_ms(c, lv)` and `n` = frames asked.
The sim's fake modem ignores it.

### Stage A: the first burst of a round and control frames (wall-clock window)

The receiver knows roughly when the answer to its poll starts (end of its own
keydown + the peer's turnaround; the carousel computes these windows already:
`s_round_ms`, `LEAD_MAX_MS`, `D_OFDM_MS`). Stage A lowers the threshold and
narrows the frequency search to `session offset ± 5 Hz` during that window.

It needs a mapping from wall-clock to the decoder's sample position. The
worker already tracks `backlog` (samples waiting in its ring,
`modem.c` `rx_worker_thread`), used to time-stamp decoded frames; use the
same arithmetic in reverse. Precision of ±100 ms is enough because the window
is hundreds of milliseconds wide.

The session's frequency offset is available from
`be->get_freq_offset(ctx, &hz)` after any decoded frame.

Do stage A only after stage B's numbers are in.

## Steps

1. Extend `utils/burst_train_probe.c` with `RX_ARMED=1` (calls
   `freedv_arm_round` on the train decoder) and print train / armed / alone.
2. Stage B in `ofdm.c`, API, hook.
3. Carousel callback and `modem.c` plumbing; switch.
4. Bench: DATAC16, DATAC15, DATAC1, QAM16C2; AWGN at the acquisition cliff and
   `ccir-poor`, `ccir-flutter`; 6-burst trains, 60 trials. False-sync control:
   the same windows with noise only, per armed threshold 0.40, 0.35, 0.30.
5. Choose `armed_thresh` per mode from the false-sync control.
6. Stage A.
7. `tests/integration` at the fringe (`MERCURY_CH_NO` near DATAC15's cliff)
   and under `ccir-poor` at +10 dB; findings document.

## Tests

- `tests/modem/test_ofdm_acq.c` (exists): add
  `test_armed_finds_weak_burst` (a train whose second burst is 3 dB weaker:
  stock misses it in most of 40 seeds, armed finds it),
  `test_armed_ignores_noise` (no second burst: no sync inside the window in
  200 seeds at the chosen threshold),
  `test_unarmed_identical` (not armed: `rx_status` sequence identical to
  trunk),
  `test_disarm_on_miss` (a missing burst leaves the decoder in stock search).
- `tests/sim`: the fake modem needs no change; add one carousel test that the
  callback is called with the right gap and count.

## Acceptance

- Not armed or switch off: identical to trunk.
- Trains at the stock acquisition cliff: armed delivers at least as many
  frames as `alone` (isolated bursts with a fresh decoder), and more than
  `train`, in every cell.
- False syncs inside an armed window with noise only: under 1 % per window at
  the chosen threshold.

## Open questions

- Multipath shifts the timing between bursts by up to the delay spread;
  ±20 ms is generous for the presets (7 ms worst case) but check
  `itur-hd`.
- Sound-card clock offset between stations is tens of ppm: under 0.5 ms over
  a burst; negligible.
- Whether armed acquisition makes the 100/200 ms gaps unnecessary is the
  subject of spec 13, not this one.
- MFSK has its own acquisition (`modem/mfsk/mfsk_sync.c`); a directed mode
  for it is a separate, later step.
