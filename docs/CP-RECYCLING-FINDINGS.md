# Reusing the cyclic prefix as a last decode attempt

Issue #19; specification `docs/improvements/19-cp-recycling.md`.
Measured 2026-11-13 on the bench only. Nothing here has been on the air.

## Question

Every OFDM symbol starts with a copy of its own last `ncp` samples, and the
receiver throws those samples away. The prefix sample and the sample one
symbol length later carry the same signal and independent noise. How much
of the noise can be averaged out of the FFT input, and can it be done
without risk to what already decodes?

## Answer

**About 0.5 dB beyond proposal 09 at the AWGN cliff of DATAC1, DATAC17 and
QAM16C2, and 0.3 to 0.5 dB on DATAC15 and DATAC3; 3 to 12 more frames in 100
at DATAC1's CCIR Poor cliff.** (DATAC1 at -1.0 dB: 10 of 100 with 09 alone,
93 with recycling; QAM16C2 at +6.5 dB: 1 of 60 against 58.) It is a later
attempt, so no cell of any table below shows fewer frames with it than
without. The prototype measured 0.5-0.8 dB with recycling inside the FFT
input of every attempt; the gains here are of the same size on DATAC1 and
QAM16C2, and smaller where acquisition, not decoding, limits the mode
(DATAC3).

Guard 4 against guard 20: on white noise they are within a few frames of each
other, 4 slightly ahead at DATAC1's lowest rows; under CCIR Poor and
disturbed NVIS they are also within noise of each other. This bench cannot
yet say that guard 20 is worth anything; it is expected to matter when the
delay spread approaches the prefix (see caveats).

## What changed

- `modem/freedv/ofdm.c`: with `cp_recycle` set (the guard, in samples), each
  of the three places where `ofdm_demod_core()` fills and transforms a row
  also builds a copy of that row in which the prefix samples are averaged
  with their twins (`ofdm_cp_recycle_row()`: the tail side with 2 samples of
  margin, the head side after the guard), transforms it into `rx_sym_cp`, and
  runs proposal 09's estimate on those rows (`ofdm_chanest_alt()` now takes
  its row buffer and phase-slope accumulator) into `rx_np_cp` / `rx_amp_cp`.
  The estimator is the instance's `chanest_alt` if on, else `t4f3` (9 or more
  carriers) or `t4`.
- `modem/freedv/freedv_700.c`: the recycled symbols are accumulated per
  packet like the 09 ones, and a packet that stock, HARQ and 09 all failed is
  decoded once more from them, behind the same parity gate and CRC check.
  `freedv_get_rx_decode_path()` returns 3 for a frame so delivered. The
  "symbols to decoded bits" sequence of the 09 and the new attempt is one
  static function.
- `freedv_set_cp_recycle()` / `freedv_get_cp_recycle()`; off by default;
  `MERCURY_CP_RECYCLE=<guard>` or `[audio] rx_cp_recycle` in the ini (the
  environment wins).
- `utils/rx_bench`: variants are `+`-joined exact tokens (`stock`, `t4`,
  `t4f3`, `t4f5`, `cpN`).

Stock, sync, unique word, SNR estimate, HARQ and the 09 attempt use what they
used before; `test_ofdm_cprecycle` asserts the stock and 09 symbols are
bit-identical with the feature on.

## Method

`utils/rx_bench` as in `docs/CHANNEL-ESTIMATION-FINDINGS.md`: one burst and
one impaired buffer per trial, decoded by every variant. Cells are
acquired/delivered per trials. The column to compare with recycling is the
estimator column beside it (`t4f5`, `t4f3` or `t4`): same estimator, same
samples. One seed, one machine, 4 benches in parallel.

## Results

### AWGN

DATAC1, per 100 (all acquired):

| SNR3k | stock | t4f5 | t4f5+cp4 | t4f5+cp20 |
|---|---|---|---|---|
| -2.0 | 0 | 0 | 0 | 0 |
| -1.5 | 0 | 0 | **59** | 6 |
| -1.0 | 0 | 10 | **93** | 86 |
| -0.5 | 0 | 87 | 97 | 97 |
| 0.0 | 0 | 98 | 100 | 100 |
| +1.0 | 78 | 100 | 100 | 100 |

DATAC3, per 100 (acquired 12, 42, 68, 96; limited by acquisition at the lower
rows):

| SNR3k | stock | t4f3 | t4f3+cp4 | t4f3+cp20 |
|---|---|---|---|---|
| -5.5 | 0 | 3 | 10 | 10 |
| -5.0 | 0 | 37 | 39 | 39 |
| -4.5 | 14 | 67 | 67 | 67 |
| -4.0 | 61 | 91 | 93 | 92 |

DATAC15 (3 carriers), per 150, `t4` and guard 4:

| SNR3k | acquired | stock | t4 | t4+cp4 |
|---|---|---|---|---|
| -12 | 95 | 3 | 23 | 44 |
| -11 | 130 | 70 | 100 | 105 |

DATAC17, per 60 (all acquired):

| SNR3k | stock | t4f3 | t4f3+cp4 | t4f3+cp20 |
|---|---|---|---|---|
| +1.5 | 0 | 38 | 59 | 59 |
| +2.0 | 0 | 57 | 59 | 59 |
| +2.5 | 0 | 60 | 60 | 60 |
| +3.0 | 32 | 60 | 60 | 60 |

QAM16C2, per 60 (all acquired):

| SNR3k | stock | t4f3 | t4f3+cp4 | t4f3+cp20 |
|---|---|---|---|---|
| +6.0 | 0 | 0 | 0 | 0 |
| +6.5 | 0 | 1 | **58** | 55 |
| +7.0 | 0 | 59 | 60 | 60 |
| +7.5 | 0 | 60 | 60 | 60 |

### Fading

DATAC1, CCIR 520 Poor (2 ms, 1 Hz), per 100:

| SNR3k (measured) | acquired | stock | t4f3 | t4f3+cp4 | t4f3+cp20 |
|---|---|---|---|---|---|
| +3.9 | 99 | 70 | 88 | 91 | 90 |
| +1.9 | 96 | 14 | 47 | 59 | 53 |

DATAC1, ITU-R F.1487 mid-latitude disturbed NVIS (7 ms, 1 Hz), per 100
(acquired 99 in each row):

| SNR3k (measured) | stock | t4f3 | t4f3+cp4 | t4f3+cp20 |
|---|---|---|---|---|
| +7.9 | 24 | 24 | 28 | 29 |
| +5.9 | 10 | 11 | 15 | 12 |
| +3.9 | 4 | 4 | 8 | 7 |

## Readings

- The gain is the noise in the FFT input, as predicted: with the window a few
  samples into a 48-sample prefix about 40 samples are averaged, a bit under
  0.8 dB of noise. It shows as 0.5 dB or so at the cliffs, which are steep
  (DATAC1: 10 to 93 of 100 in 0.5 dB).
- Nothing is lost anywhere: in every row recycling is greater than or equal
  to the same estimator without it, and "any" equals the best column. The one
  point where the prototype lost 2 of 100 (DATAC1, 0.0 dB) shows 98 against 100
  here, as expected of a later attempt.
- The gain is small on DATAC3 below -5 dB because those rows are limited by
  acquisition (12 and 42 of 100), which recycling does not touch.
- On disturbed NVIS, where 09 did nothing, recycling gains a few frames
  (24 to 28 or 29, 4 to 8): the limit there is mostly intersymbol
  interference, but some of the loss is plain noise.
- DATAC1 at -1.5 dB: guard 4 delivers 59 and guard 20 only 6. On white noise
  the longer guard costs most of the head-side samples and with them about
  0.2 dB; at a steep cliff that is the difference. The guard should follow the
  delay spread (proposal 10), not be set long as a precaution.

## Cost

Per modem frame, eight more `dft()` calls (`m x (nc+2)` multiply-adds each)
plus the estimate and a copy, only when the feature is on. One more LDPC
decode on a packet that every earlier attempt failed, the expensive case
(`docs/CHANNEL-ESTIMATION-FINDINGS.md`, "Cost"): about 60 ms
(DATAC3) to 235 ms (QAM16C2) on one 2.1 GHz Xeon core, against an allowance of
about 1 s. **Not timed on a Raspberry Pi**; it comes on top of the 09 attempt,
so proposal 23 must settle the budget before either becomes a default. With
effort levels, `normal` could skip the 09 attempt and go straight to this
one, which the tables above favour (the cp column is never behind the t4
column).

## Caveats

- Bench only, one seed, 60-150 trials a point.
- No frequency offset in any run. Pairs stay aligned under an offset because
  the down-conversion uses the absolute sample index, but this is untested.
- The delay-spread cases are thin: CCIR Poor (2 ms) and disturbed NVIS (7 ms)
  only, DATAC1 only. `ccir-moderate`, `ccir-flutter`, DATAC16 and the other
  rungs under fading were not run. The unit test's two-path channel (16 samples
  at 0.7) loses nothing at guard 20, but also gains almost nothing
  (20 against 21 of 32), so the case for guard 20 is argued, not measured.
- Guard and margins (2 samples on the tail side) were not swept (spec
  step 6: 2, 4, 8).
- False accepts: another chance at the 2^-16 CRC escape. It runs behind the
  parity gate, but the noise-only and wrong-session controls (1000 windows per
  mode) have not been run.
- The LLR scale (`EsNodB`) was not re-tuned for the recycled symbols, whose
  noise is lower.
- Not timed on a Raspberry Pi. Not tried on the air.
- Acceptance numbers: the 90 % point is not interpolated from these rows;
  the rows above show gains of 0.5 dB or more on DATAC1, DATAC17 and
  QAM16C2, but a run with a finer step is needed to state it.

## Reproducing

```sh
make -C modem/freedv libfreedvdata.a && make -C utils rx_bench
cd utils
./rx_bench DATAC1  100  -2    0   0.5 awgn      stock,t4f5,t4f5+cp4,t4f5+cp20
./rx_bench DATAC1  100   0    1   1   awgn      stock,t4f5,t4f5+cp4,t4f5+cp20
./rx_bench DATAC1  100 -34  -32   2   ccir-poor stock,t4f3,t4f3+cp4,t4f3+cp20
./rx_bench DATAC1  100 -38  -34   2   itur-mdv  stock,t4f3,t4f3+cp4,t4f3+cp20
./rx_bench QAM16C2  60   6    7.5 0.5 awgn      stock,t4f3,t4f3+cp4,t4f3+cp20
./rx_bench DATAC3  100  -5.5 -4   0.5 awgn      stock,t4f3,t4f3+cp4,t4f3+cp20
./rx_bench DATAC15 150 -12  -11   1   awgn      stock,t4,t4+cp4
./rx_bench DATAC17  60   1.5  3   0.5 awgn      stock,t4f3,t4f3+cp4,t4f3+cp20
make -C tests test_ofdm_cprecycle && ./tests/test_ofdm_cprecycle
```
