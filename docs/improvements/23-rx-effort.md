# 23 — Decode-time budget and effort levels (issue #23)

**Kind:** receiver only. No wire change. **Depends on:** nothing; it gates
09 becoming a default and every later fallback attempt (19, 20, 05, 12, 22).
**Switch:** `MERCURY_RX_EFFORT` / `[audio] rx_effort = low|normal|high|max`,
default `low` (today's behaviour).

## Goal

Make the list of decode attempts fit the machine it runs on: measured costs,
hopeless decodes stopped early, and a per-frame time budget.

## Facts

- Time allowed: `D_OFDM_MS` 1000 and `D_MFSK_MS` 4500 in
  `datalink_arq/carousel.c` ("the latest S decodes an M keydown"). The slot
  arithmetic of the turn-taking rules is built on them (`specs/turns`).
- Measured, one 2.1 GHz Xeon core, a decode that fails (100 iterations):
  DATAC3 about 60 ms, DATAC1 about 100 ms, DATAC17 about 210 ms, QAM16C2
  about 235 ms. A decode that converges takes a small fraction of that.
- Each fallback attempt on a failed frame is another full LDPC run. With 09
  alone a failed frame costs twice the above.
- Raspberry Pi 4: not measured for these modes. The in-tree figures are for
  MFSK (0.16-0.4 s per decode, `docs/CAROUSEL-ARQ.md`).
- More iterations are not worth their time: 400 or 1600 instead of 100 move
  DATAC1's cliff by about 0.1 dB (research note 3.3).
- `SumProduct()` (`modem/freedv/mpdecode_core.c:365`) knows, on every
  iteration, `ssum`, the number of satisfied parity checks; it stops only on
  all checks satisfied or at `max_iter`. The per-code limits are in the
  `H_*.h` files (`*_MAX_ITER` 100) and `ldpc->max_iter`.
- Decoding runs in the per-plane worker threads of `modem/modem.c`
  (`rx_worker_thread`); the control and payload planes are separate so a
  slow payload decode cannot starve control.
- `utils/rxcost_bench.c` measures the idle receive chain per mode;
  `utils/idle_cpu_probe.c` the idle station.

## Design

### 1. Measure

Extend `rxcost_bench` (or add `utils/decode_cost.c`): per mode, on seeded
bursts at three SNRs (well above, at, and below the cliff), the CPU time of
the first decode and of each fallback attempt, mean and worst case. Run it
on a Pi 4, a Pi 5 and an x86 desktop and record the table in the findings.

### 2. Stop hopeless decodes early

In `SumProduct()`, optional (a field in `struct LDPC`, default off):

```
keep best = max ssum seen so far and the iteration it was reached
if iter >= MIN_ITERS and iter - best_iter >= STALL and best < FRAC * NumberParityBits:
    stop
```

Start with `MIN_ITERS` 15, `STALL` 10, `FRAC` 0.97 and tune on the bench.
The criterion must be shown to lose no frame: run every mode at its cliff
with and without it on the same samples (`rx_bench`), 200 trials, and
require identical delivered sets. Report the time saved on failed frames.

Because iterations beyond about 100 buy nothing, also measure whether
`max_iter` 50 loses anything; if not, lower it for the fallback attempts.

### 3. Budget

Per mode, a budget in milliseconds for everything after the first decode:

```
budget(mode, effort) = min( allowance(mode) − first_decode_worst(mode) − margin,
                            effort_cap )
```

Attempts are tried in a fixed order of expected value per unit time (from
the bench tables of each proposal) until the next one's worst-case time
would exceed what is left. Skipped attempts are counted and logged once per
session, so an operator can see that a faster machine would decode more.

The worst-case times come from a short self-calibration at start-up (decode
a stored noisy frame of each mode once), not from compiled-in constants, so
the same binary adapts to a Pi and a desktop.

### 4. Effort levels

| level | attempts after the first decode |
|---|---|
| `low` | none beyond today's (HARQ combine where it applies) |
| `normal` | the single best fallback per mode that fits the budget |
| `high` | every measured-useful fallback that fits |
| `max` | as `high`, with fallbacks on a helper thread per core and the research attempts (20's extra rounds, 12's higher orders, 21's larger candidate lists) |

Which attempts each level contains is a table in one place
(`modem/rx_effort.c`), filled from the findings of each proposal.

### 5. Threads

At `high` and above, run fallback attempts on a helper thread fed with the
packet's saved buffers, so the plane's worker returns to demodulating the
next burst immediately. The result is delivered through the same path as a
normal decode, with its end time, so the carousel's timing is unchanged.
Mind the instance locks (`modem_inst_lock_for`): the helper needs its own
decoder state or the saved buffers must be self-contained.

## Steps

1. Timing tool and the three-machine table.
2. Early termination with the no-loss check.
3. Start-up calibration, budget, effort levels, logging.
4. Helper thread.
5. `tests/integration` with `rx_effort = high` under ASan and TSan.
6. Findings document with the table and the recommended default per class
   of machine.

## Tests

- `rx_effort = low` is bit-identical to trunk.
- Early termination: identical delivered sets on seeded cliff runs for every
  mode; a decode of pure noise stops within 30 iterations.
- Budget: with an artificially small budget no fallback runs; with a large
  one all do; the decode never finishes later than the allowance in a
  timing test with a deliberately slowed decoder.
- TSan clean with the helper thread.

## Acceptance

- On a Pi 4 at `normal`, the worst-case time from end of burst to decoded
  frame is within the allowance for every mode, measured.
- Failed-frame decode time reduced by at least half by early termination,
  with no frame lost on the bench.
- The findings state, per machine class, which attempts fit.

## As built, step 2 only (2026-11-14)

Findings: `docs/RX-EFFORT-FINDINGS.md`.

- Early termination: `SumProductStall()` in `mpdecode_core.c`, `struct LDPC`
  field `early_stop`, `freedv_set_ldpc_early_stop()`,
  `MERCURY_RX_EARLY_STOP=1` / `[audio] rx_early_stop`. It applies to every
  decode of the instance, the stock one included.
- The rule is 15 / 10 / **0.80**, not the 0.97 above: with 0.97 DATAC1 lost
  3 frames of 37 at the cliff (handbook pitfall 17). It applies only to
  codes of at least 2048 parity checks: on the short codes it lost frames
  at any threshold tried.
- `rx_bench` prints the CPU time per trial of each variant with
  `RX_BENCH_TIME=1`.
- Not built: the three-machine timing table (step 1; x86 only), the budget,
  the effort levels, the helper thread. The MFSK decoder has its own LDPC
  and is not covered.
