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

### 1b. Power-inverter noise (requested by the fork owner)

Cheap solar and battery inverters, and the switching supplies around them,
are now the commonest man-made noise in residential areas and RV/trailer
parks. It is not one kind of noise but a mixture, and each part is answered
by a different proposal, so the bench must be able to produce the parts
separately and together:

| component | what it looks like in a 2.4 kHz receive passband | answered by |
|---|---|---|
| switching harmonics | one or more carriers, drifting slowly (a few Hz per minute, faster while the load changes) and wobbling by a few Hz | 07 (per-carrier weights, notch) |
| mains modulation | each carrier amplitude-modulated at twice the mains frequency (100 or 120 Hz), so it has sidebands at multiples of that | 07 |
| commutation buzz | broadband noise gated at 100/120 Hz, duty 10-50 %: the "buzz" of modified-sine inverters | 08 (periodic blanking), 07 (per-frame weights) |
| dithered switching | a hump of raised noise a few hundred Hz wide instead of a clean carrier (spread-spectrum PWM) | 07 |
| several units | two or more of the above with slightly different frequencies, beating | all of them at once |

```c
typedef struct {
    int    n_carriers;        /* 0..4 */
    double carrier_hz[4];     /* audio frequency of each harmonic */
    double carrier_db[4];     /* level relative to the signal */
    double drift_hz_per_s;    /* slow linear drift */
    double wobble_hz;         /* peak random-walk deviation */
    double mains_hz;          /* 50 or 60; modulation is at twice this */
    double am_depth;          /* 0..1 amplitude modulation of the carriers */
    double buzz_db;           /* gated broadband noise, relative to the signal's rms; <= -90 = none */
    double buzz_duty;         /* fraction of each half mains cycle */
    double hump_hz, hump_width_hz, hump_db;   /* dithered-switching noise hump */
} inverter_profile_t;

void impair_inverter(int16_t *x, int n, double fs, const inverter_profile_t *p, bench_rng *rng);
const inverter_profile_t *impair_inverter_preset(const char *name);
```

Presets to start with (names are ours; the parameters are educated guesses
until calibrated): `inv-buzz` (modified sine: buzz only, 30 % duty),
`inv-carrier` (one PWM harmonic with 120 Hz modulation and drift),
`inv-comb` (three harmonics 400-700 Hz apart), `inv-dither` (hump only),
`inv-mix` (two units: `inv-buzz` at 60 Hz plus `inv-carrier`),
`powerline` (the older case: narrow impulses at 100/120 Hz).
`rx_bench --inverter NAME[,DB]` adds one; the option may be repeated to mix.

**These are synthetic models, not measurements.** Before any claim that a
mitigation "removes inverter noise", record the real thing with
`MERCURY_RX_DUMP` near actual inverters (several makes, idle and loaded),
replay it with `rx_replay`, and tune the presets until the bench reproduces
what the recordings do to each mode. Keep the recordings' provenance in the
findings document.

### 2. `utils/rx_bench.c` — the multi-variant sweep

A first stage exists (`utils/rx_bench.c`, added with proposal 09): AWGN or a
Watterson preset, a tone, random impulses, and the channel-estimate variants
`stock,t4,t4f3,t4f5`. The rest of this section is what it should grow into.

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
