# 19 — Reuse the cyclic prefix (issue #19)

**Kind:** receiver only. No wire change. **Depends on:** 09 (the fallback
attempt list), 00-bench.
**Switch:** `MERCURY_CP_RECYCLE` / `[audio] rx_cp_recycle`, default off.

## Goal

Stop discarding the cyclic prefix's energy when the channel does not need
all of it: 0.5-0.8 dB on top of proposal 09 (measured on the prototype).

## Facts

- An OFDM symbol on the air is `ncp` samples of prefix followed by `m`
  samples (`struct OFDM`: `m`, `ncp`, `samplespersymbol = m + ncp`). At
  8 kHz, `m` = 128 (16 ms) and `ncp` = 48 (6 ms) for the DATAC modes, 32
  (4 ms) for QAM16C2. The prefix is a copy of the symbol's last `ncp`
  samples, so **transmitted sample t equals transmitted sample t + m for
  every t inside the prefix.**
- `ofdm_demod_core()` (`modem/freedv/ofdm.c`) takes, for each row it needs,
  `m` samples starting at `st = ... + 1 + ofdm->sample_point`, de-rotates
  them by the frequency estimate (`work[k] = rxbuf[j] * cmplxconj(woff_est *
  j)`) and transforms them (`dft()`). There are three such places: the
  previous pilot, the loop over this frame's rows up to the next pilot, and
  the future pilot.
- `sample_point` is kept between `timing_est + 4` and `timing_est + ncp − 4`,
  where `timing_est` is the estimated start of the prefix. So
  `delta = sample_point − timing_est` is how far into the prefix the FFT
  window starts.
  - The window's **first** `ncp − delta` samples are still prefix; their
    copies lie just **after** the window (`+m`), in the symbol's own tail,
    untouched by any other symbol.
  - The window's **last** `delta` samples have their copies just **before**
    the window (`−m`), in the first `delta` samples of the prefix. The first
    of those are where the previous symbol's echoes land (delay spread, and
    the ringing of the transmit and receive filters).
- The de-rotation uses the absolute sample index, so a pair of copies stays
  aligned under a frequency offset.
- Averaging a pair halves the noise on that sample. With `c` samples
  averaged, the noise in the FFT falls by `m / (m − c/2)`: at most 0.9 dB
  for `ncp` 48, 0.6 dB for `ncp` 32.

## Prototype

`prototype/lab2/lab2-prototype.diff`, function `mx_cp_recycle()`: after each
of the three fills,

```c
int delta = ofdm->sample_point - ofdm->timing_est;       /* clamped to 0..ncp */
int a = ncp - delta - 2;                  /* tail side, 2 samples of margin  */
int b = delta - guard;                    /* head side                       */
for (k = 0; k < a; k++)      work[k] = 0.5f * (work[k] + r(st + k + m));
for (k = m - b; k < m; k++)  work[k] = 0.5f * (work[k] + r(st + k - m));
/* r(j) = rxbuf[j] * cmplxconj(woff_est * j), with bounds checks on j */
```

Variants measured: tail side only (`cp1`); both sides with `guard` 4 samples
(`cp2`) or 20 samples = 2.5 ms (`cp3`). Results in the research note,
section 3.1. `cp2` was best on white noise, `cp3` on CCIR Poor (2 ms delay),
and `cp2` lost 2 frames in 100 at one point where the un-recycled attempt
delivered them.

## Design

Recycling changes the FFT input, so it cannot share the stock attempt's
symbols. Compute a second set of rows.

1. In `struct OFDM`: `int cp_recycle;` (0 off, else the head-side guard in
   samples) and a second row buffer `rx_sym_cp` with the same shape as
   `rx_sym`.
2. In `ofdm_demod_core()`, when on: after each stock `dft()`, apply the
   averaging to a copy of `work` and `dft()` it into `rx_sym_cp`. (Each
   `dft()` is `m × (nc + 2)` multiply-adds; eight more per modem frame is
   negligible.)
3. Run proposal 09's alternative estimate on `rx_sym_cp` as well, into
   `rx_np_cp` / `rx_amp_cp`; `freedv_700.c` accumulates them per packet
   like `rx_syms_alt`.
4. Attempt order on a packet (stop at the first CRC pass): stock; HARQ
   combine; alternative estimate (09); **alternative estimate on recycled
   rows**. With effort levels (proposal 23), `normal` may skip the third and
   go straight to the fourth, which the measurements favour.
5. Guard choice: start with two builds of the fourth attempt, guard 4 and
   guard 20, and take the measurement's verdict per mode; later choose from
   the delay-spread estimate of proposal 10.
6. Margins against timing jitter: the tail side keeps 2 samples, the head
   side its guard. Sweep 2, 4, 8 on the bench at each mode's cliff; the
   timing estimate is noisiest exactly where this feature matters.

API: `freedv_set_cp_recycle(f, guard_samples)`; backend hook or the same
`fdv_open` path as proposal 09; `rx_bench` variants `cp4`, `cp20`.

## Tests (`tests/modem/test_ofdm_cprecycle.c`)

- `test_off_is_identical`: off, decoded bytes and status sequence as trunk.
- `test_recycled_rows_match_clean`: with no noise, every recycled row equals
  the stock row to within float error (the pairs really are copies).
- `test_adds_frames_at_cliff`: DATAC3, white noise, seeded, 16 bursts at the
  cliff of proposal 09: at least 3 more delivered, none lost.
- `test_delay_spread_guard`: a two-path channel with 2 ms delay: guard 20
  loses no frame that the un-recycled attempt delivers.

## Bench for the findings document

`rx_bench`, all rungs and DATAC16; AWGN, `ccir-poor`, `ccir-moderate`,
`itur-mdv`, `ccir-flutter`; variants stock, 09, 09 + recycling at guard 4 and
20; a ±20 Hz offset row; the noise-only control.

## Acceptance

- Off: identical to trunk.
- On: delivered ≥ the same build without it on every cell (it is a later
  attempt; a shortfall is a bug).
- White noise: 90 % point at least 0.4 dB lower than with 09 alone for
  DATAC1, DATAC3 and DATAC17, at least 0.3 dB for QAM16C2.
- False accepts: zero in 1000 noise-only windows per mode.
- Decode-time budget respected on the target machine (proposal 23).

## Later: an adaptive guard

Proposal 26 measures the delay profile on the pilot rows. The clean part of
the prefix is then known (from the last path's prefix start to the first
path's useful start) and can replace the fixed guard of this proposal.
