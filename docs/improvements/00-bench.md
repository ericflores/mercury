# 00 — Bench tooling for receiver work (issue #17)

**Kind:** test tooling only. No behaviour change in `mercury`.
**Do this first:** proposals 09, 07, 08, 03 and 06 are judged with it.

## Goal

Turn the scratch harness (`prototype/rxlab.c`) into proper instruments, so
receiver features are compared on identical samples, with interference and
impulse noise the existing tools cannot generate, and against recordings from
the air.

## What exists

- `utils/bench_common.{c,h}`: RNG streams, `bench_fill_payload`,
  `bench_make_burst`, `bench_run_burst[_ex]`, `bench_mode_from_name`.
- `utils/chanutil.{c,h}`: `chanutil_open(preset, no_dbhz, seed)`,
  `chanutil_run(ch, pb, n, &snr3k)`; presets by name.
- `utils/acquire_vs_decode.c`: the reference single-variant sweep.
- `MERCURY_RX_DUMP=<file>` (`modem/modem.c:785-815`): the running modem writes
  every received chunk as raw s16le at 8 kHz, with a log mark once a second.

Missing: co-channel interference, impulse noise, a multi-variant column
layout, and offline replay of a dump.

## Deliverables

### 1. `utils/impair.{c,h}` — impairments on a real int16 buffer

```c
/* All add to x[0..n) in place, clamp to int16, and take the caller's RNG. */
void impair_tone(int16_t *x, int n, double fs, double hz, double amp, double phase);
void impair_fsk(int16_t *x, int n, double fs, double hz, double shift_hz,
                double baud, double amp, bench_rng *rng);          /* RTTY/FT8-like */
void impair_impulses(int16_t *x, int n, double fs, double rate_hz,
                     double width_ms, double amp, bench_rng *rng); /* Poisson */
void impair_impulses_periodic(int16_t *x, int n, double fs, double period_hz,
                              double width_ms, double amp, double jitter_ms,
                              bench_rng *rng);                     /* 100/120 Hz */
```

Levels are passed as linear amplitudes; the tool converts from dB relative to
the burst's measured power so tables can say "−3 dB relative to the signal".
A fixed number of RNG draws per sample, whether or not an impulse is active,
so variants and rows stay paired.

### 2. `utils/rx_bench.c` — the multi-variant sweep

Usage (keep it close to `acquire_vs_decode`):

```
rx_bench MODE trials lo hi step channel [--variants LIST] [--tone HZ,DB]
         [--fsk HZ,SHIFT,BAUD,DB] [--impulse RATE,WIDTH_MS,DB] [--periodic HZ,WIDTH_MS,DB]
         [--offset HZ] [--seed N] [--csv]
```

- Per trial: payload → burst → normalise to `HEADROOM_PEAK` → channel
  (`chanutil` or AWGN) → impairments → one buffer (0.4 s noise lead-in, the
  burst, one burst length of noise tail).
- Each variant has its own `struct freedv *` configured **through the public
  setters** added by the feature under test (`freedv_set_chanest_mode`,
  `freedv_set_noise_weight`, ...), never through globals.
- Each variant is fed the same buffer with `bench_run_burst(rx, buf, ntot, 0,
  0.0, out, zero_noise, ...)`.
- Delivered requires CRC ok **and** `memcmp(out, payload, nbytes - 2) == 0`.
- Columns: `acquired/delivered` per variant, then `any`.
- `fflush(stdout)` after every row.
- A `--noise-only` mode: the same windows with no burst, counting sync and
  CRC-ok events per variant (the false-accept control).

Variant syntax: names, not numbers, for example
`stock,chanest=t4f3,chanest=t4f3+noisew,blank=6`. Document the list in the
tool's header comment.

Add it to `OUT` in `utils/Makefile` with the same recipe as
`acquire_vs_decode` (line 77).

### 3. `utils/rx_replay.c` — decode a recording with each variant

```
rx_replay FILE.s16 MODE [--variants LIST] [--seed-crc HEX]
```

Reads raw s16le 8 kHz (a `MERCURY_RX_DUMP` file), feeds it to one decoder per
variant honouring `freedv_nin`, and prints for each variant the sample offset
of every sync and every CRC-ok frame. Carousel frames carry a seeded CRC
(`freedv_set_crc_seed`), so the tool takes the session seed from the log
(`--seed-crc`) or counts "parity converged" frames when it is unknown.

This is the bridge to real noise: an operator sets `MERCURY_RX_DUMP`, records
a session with QRM or static, and the file becomes a repeatable test.

### 4. Calibration test

`tests/integration` already runs tool tests (see
`channel_bridge_test.go`). Add one that runs `rx_bench DATAC16 ...` at two
AWGN points with the stock variant and checks the counts against
`acquire_vs_decode` for the same seed, so the new tool cannot drift.

## Acceptance

- `rx_bench MODE N lo hi 1 awgn --variants stock` matches
  `acquire_vs_decode MODE N lo hi` within run-to-run noise for DATAC16,
  DATAC15, DATAC1 and QAM16C2.
- Two variants that are both `stock` produce identical columns (pairing
  works).
- `make -C utils` is warning-free under `-Wall -Wextra`.

## Notes

- A fading point costs minutes for the long codes; print progress.
- Keep `HEADROOM_PEAK` at 4000 as the other tools do.
- The tone level convention in the prototype was "dB relative to total signal
  power"; keep it and state it in every table.
