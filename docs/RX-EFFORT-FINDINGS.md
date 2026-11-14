# The cost of a frame that fails: early termination of hopeless decodes

Issue #23, step 2; specification `docs/improvements/23-rx-effort.md`.
Measured 2026-11-14 on the bench, one x86 core. Nothing here has been on
the air, and no Raspberry Pi has been timed.

## Question

A frame that fails spends the LDPC decoder's full 100 iterations finding
that out, and every optional decode attempt (proposals 09, 19, 26, 07, 20)
pays that again. Can a decode that is going nowhere be stopped early
without losing frames that would have decoded?

## Answer

**On the long codes, yes, and it halves to thirds the cost of a failed
frame; on the short codes, no, and the rule is not applied to them.**

CPU seconds per trial of the receiver on white noise below the cliff (every
frame fails), stock attempt only and with the attempts of 09, 19, 26 and
07:

| mode | stock | stock, early stop | attempts | attempts, early stop |
|---|---|---|---|---|
| DATAC1 at −3 dB | 0.18 | 0.12 | 0.42 | **0.18** |
| DATAC17 at 0 dB | 0.38 | 0.19 | 1.11 | **0.35** |

With it on, the full list of attempts on a failed DATAC1 frame costs what
the stock receiver's single failed decode costs today.

**It is not free of loss.** Over the long-code cells of the sweep below it
lost 2 frames in about 950 delivered by the stock attempt and 1 in about
1300 delivered with the fallback attempts on. It makes the receiver
cheaper, not better: it is for a machine that cannot afford the attempts
otherwise.

## The rule, and why it is not the specification's

A decode is abandoned once 15 iterations have run, its best count of
satisfied parity checks has not improved for 10 iterations, and that best
is under **80 %** of the checks. Only for codes with at least 2048 checks.

- The specification said 97 %. At DATAC1's cliff that lost 3 frames of 37
  (40 trials at +1 dB): a decode that ends up converging can sit between
  80 and 97 % for more than ten iterations first. 90 % lost 3 of 30. 80 %
  lost none there, for the same time saved (a hopeless decode's best count
  is under 80 %).
- On the short codes the count is too noisy at any threshold tried. With
  the rule on (80 %), per 200: DATAC3 (1024 checks) at −4 dB 127 → 116 and
  at −4.5 dB 25 → 19; DATAC15 at −11 dB 93 → 82; DATAC4 at −9 dB 139 → 132;
  and with the fallback attempts DATAC15 at −11.5 dB 98 → 92. Their decodes
  are the cheap ones (0.08-0.15 s a trial, hardly changed by the rule), so
  the rule skips them: `STALL_MIN_PARITY` 2048.

## What changed

- `modem/freedv/mpdecode_core.c`: `SumProductStall()`, the sum-product loop
  with the stall test; `struct LDPC` gains `early_stop` (0 in the code
  table). `LDPC_ES_MIN`, `LDPC_ES_STALL`, `LDPC_ES_FRAC` in the environment
  override the three values, for bench sweeps only.
- `freedv_set_ldpc_early_stop()`; it applies to every decode of the
  instance, the stock one included.
- `MERCURY_RX_EARLY_STOP=1` / `[audio] rx_early_stop`, off by default.
- `utils/rx_bench`: token `es`; `RX_BENCH_TIME=1` prints the CPU seconds per
  trial each variant's receiver took.

`tests/modem/test_ldpc_early_stop.c`: off, a decode of noise runs to the
limit; on, it stops within 30 iterations on the long codes and runs to the
limit on a short one; a decode that converges is identical either way.

## Results (rule 15 / 10 / 80 %, before the short codes were excluded)

Delivered per the number given; `cpu` is seconds per trial; the machine was
running other benches, so the times are for comparing columns, not
absolute.

### Long codes

DATAC1 (4096 checks), white noise, per 200:

| SNR3k | stock | early stop | 09 + 19 | 09 + 19, early stop | cpu, same order |
|---|---|---|---|---|---|
| 0.0 | 0 | 0 | 199 | 199 | 0.19 / 0.11 / 0.23 / 0.16 |
| +0.5 | 4 | **3** | 199 | 199 | 0.19 / 0.13 / 0.22 / 0.16 |
| +1.0 | 172 | **171** | 198 | 198 | 0.15 / 0.15 / 0.16 / 0.16 |
| +1.5 | 198 | 198 | 200 | 200 | 0.13 / 0.12 / 0.13 / 0.13 |

A second run of two of these points (another random sequence): 3 / 3 and
165 / 165; 200 / 200 with the attempts.

DATAC1, CCIR Poor, per 200:

| SNR3k | stock | early stop | 09 + 19 | 09 + 19, early stop | cpu |
|---|---|---|---|---|---|
| +6.0 | 188 | 188 | 191 | 191 | 0.12 / 0.12 / 0.13 / 0.13 |
| +4.0 | 140 | 140 | 177 | 177 | 0.15 / 0.13 / 0.17 / 0.15 |
| +2.0 | 34 | 34 | 123 | **122** | 0.19 / 0.14 / 0.27 / 0.18 |
| −0.1 | 0 | 0 | 29 | 29 | 0.19 / 0.14 / 0.32 / 0.18 |

DATAC1, disturbed NVIS, with 09 + 19 + 26, per 100:

| SNR3k | stock | early stop | attempts | attempts, early stop | cpu |
|---|---|---|---|---|---|
| +7.9 | 24 | 24 | 93 | 93 | 0.18 / 0.13 / 0.31 / 0.18 |
| +5.9 | 10 | 10 | 81 | 81 | 0.18 / 0.12 / 0.35 / 0.19 |
| +3.9 | 4 | 4 | 55 | **54** | 0.18 / 0.12 / 0.39 / 0.19 |

QAM16C2 (6480 checks), CCIR Poor, per 60:

| SNR3k | stock | early stop | 09 + 19 | 09 + 19, early stop | cpu |
|---|---|---|---|---|---|
| +13.5 | 50 | 50 | 51 | 51 | 0.10 / 0.10 / 0.11 / 0.10 |
| +11.4 | 38 | 38 | 42 | 42 | 0.14 / 0.11 / 0.19 / 0.12 |
| +9.4 | 15 | 15 | 23 | 23 | 0.19 / 0.10 / 0.35 / 0.15 |

DATAC17 CCIR Poor (+14, +12, +10 dB, per 60) and DATAC17 and QAM16C2 on
white noise at the points run (all above their cliffs: 100 of 100): equal
in every column.

### Short codes (why they are excluded)

Per 200, white noise:

| mode, SNR3k | stock | early stop | 09 + 19 | 09 + 19, early stop |
|---|---|---|---|---|
| DATAC3, −4.5 | 25 | **19** | 142 | 142 |
| DATAC3, −4.0 | 127 | **116** | 186 | 186 |
| DATAC15, −12.0 | 5 | **2** | 51 | **48** |
| DATAC15, −11.5 | 23 | **17** | 98 | **92** |
| DATAC15, −11.0 | 93 | **82** | 141 | 141 |
| DATAC4, −9.0 | 139 | **132** | 163 | 163 |
| DATAC16, −11.0 | 135 | 134 | 139 | 139 |

## Caveats

- Bench only, one seed, one x86 core under other load; no Raspberry Pi.
- The long-code cliffs of DATAC17 and QAM16C2 on white noise were not hit
  by the sweep (its points sat above them), so their no-loss evidence is
  the fading rows.
- After the short codes were excluded only the unit test was re-run; the
  long-code rows above are from before the exclusion, which does not
  change them.
- The MFSK floor has its own LDPC decoder and is not covered.
- The rest of proposal 23 (the timing table on three machines, the budget,
  the effort levels, the helper thread) is not built.

## Reproducing

```sh
make -C modem/freedv libfreedvdata.a && make -C utils rx_bench
cd utils
RX_BENCH_TIME=1 ./rx_bench DATAC1 200 0.0 1.5 0.5 awgn stock,es,t4f3+cp4,t4f3+cp4+es
RX_BENCH_TIME=1 ./rx_bench DATAC1 200 -36 -30 2 ccir-poor stock,es,t4f3+cp4,t4f3+cp4+es
RX_BENCH_TIME=1 ./rx_bench DATAC17 40 0 0 1 awgn stock,es,t4f3+cp4+pt+nw,t4f3+cp4+pt+nw+es
LDPC_ES_FRAC=0.97 ./rx_bench DATAC1 40 1 1 1 awgn stock,es     # the specification's value
make -C ../tests test_ldpc_early_stop && ../tests/test_ldpc_early_stop
```
