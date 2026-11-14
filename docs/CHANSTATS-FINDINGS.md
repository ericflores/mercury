# What is limiting the link: Doppler, delay, EVM and a first classification

Issue #10; specification `docs/improvements/10-channel-telemetry.md`.
Measured 2026-11-14 on the bench only (synthetic pilots, the project's
Watterson model, AWGN). Nothing here has been on the air, and nothing in
the receiver or the ARQ reads any of it: it is logging.

## Question

Can the receiver say, from one packet, *what* is degrading the link (white
noise, a carrier, delay spread beyond the prefix, fast fading, a distorted
transmitter) and not only *how much* (the SNR)? The proposal asks for a
Doppler estimate (`dopp`), a delay estimate (`dly`), the data symbols' error
(`evm`) and its excess over what the pilot noise predicts (`evm_excess`), and
a rule list that turns them into a class.

## Answer

**Partly.** Measured, per packet, on DATAC1 at 10 dB over its rung threshold:

| condition | frames called correctly | what it was called otherwise |
|---|---|---|
| AWGN | 60 of 60 (clean) | |
| a tone 10 dB under the burst | 60 of 60 (narrowband) | |
| ITU-R F.1487 CCIR poor (2 ms, 1 Hz) | 60 of 60 (clean: nothing limits it) | |
| CCIR flutter (0.5 ms, 10 Hz) | 27 of 27 (doppler); only 27 of 60 bursts reached a result | |
| disturbed NVIS (7 ms, 1 Hz) | **0 of 58** (should be isi) | noise, 58 |
| transmitter clipped harder (gain 5.0 against 2.7) | 58 of 60 (txdist) | clean, 2 |
| impulses | **not built** (the blanker's fraction and the gap statistics are not there) | |

- `dopp` and `dly` separate what the acceptance asks: `dly` reads 0.56 /
  1.97 ms on the 0.5 / 2 ms presets (and 2.97 ms on a 3 ms preset, 1.01 on a
  1 ms one); `dopp` reads 0.43-1.07 Hz on the 0.1-1 Hz presets and its
  maximum (8.5 Hz) on 10 Hz. They are coarse (below).
- **`dly` does not read the 7 ms disturbed NVIS channel** (median 0.0 ms,
  10 to 90 % range 0 to 6.6 ms), and on that channel nothing else in the
  list sees the delay spread either: the data symbols' error is explained by
  the pilots' noise (ISI hits the pilots as it hits the data), so
  `evm_excess` is 0 and the SNR estimate (2.3 dB at an SNR3k of 12.8)
  makes it "noise". The proposal's ISI rule does not fire. The case the
  proposal names as the first policy candidate (`docs/FADE-CLIFF-DECISION.md`)
  is the one these estimators miss.
- **The stock transmitter's own clipper puts a floor under the EVM** and so
  under any "excess": DATAC1 on a perfect channel is at -12.9 dB EVM, so
  `evm_excess` as specified reads 3.5 dB at an SNR3k of 13, 8.5 at 20 and
  17.7 at 30 on AWGN. With the clipper off it is 0 to 0.5 dB at every SNR
  from 8 to 30 dB. The classification therefore reads `evm_resid`, the
  excess over the noise *and* the mode's measured floor (0 on AWGN at every
  SNR).

## What changed

All of it behind `MERCURY_CHANSTATS=1` / `[main] chanstats` (the noise
statistics switch), off by default; with it off nothing is accumulated or
logged (`test_off_is_zero`, `test_decode_unchanged`: the same bytes, on and
off, on the same samples).

- `modem/freedv/ofdm.c`: `ofdm_chan_acc_*`, accumulated per burst beside the
  noise profile, never in a profile replay (the replay runs on a copy of the
  struct that shares pointers: same guard as `ofdm_noise_profile()`).
- `modem/freedv/freedv_api.{c,h}`: `struct freedv_rx_frame_stats` gains
  `dopp_hz`, `dly_ms`, `evm_db`, `evm_excess_db`, `evm_noise_db`,
  `evm_floor_db`, `evm_resid_db`, `cp_ms`, `chan_valid`.
- `modem/chan_classify.{c,h}`: the classification as a pure function.
- `modem/modem_freedv.c`: the `chanstats:` line gains, at the end,
  `dopp= dly= evm= evmx= evmr= limit=`. Existing keys and their order are
  unchanged.
- `utils/chanstats_bench.c`: the bench behind every table here.
- Tests: `tests/modem/test_chanstats.c` (estimators on synthetic pilots, the
  classification, AWGN EVM, off, decode unchanged).

How the estimators work (all from the pilot rows, 5 symbols = 0.11 s apart
for the 6 ms-prefix modes, carriers 62.5 Hz apart):

- **`dopp`.** For a Gaussian Doppler spectrum with spread 2 sigma, the
  correlation of one carrier's pilot with the pilot one row later is
  `rho = exp(-2 pi^2 sigma^2 T^2)`, so two rows later it is `rho^4`. The
  lag-1 and lag-2 sums (pooled over carriers and frames) are free of noise
  bias, and their ratio is `rho^3` whatever the signal power. Where the lag-2
  sum is lost in the noise (a spread over about 4 Hz), the lag-1 sum over the
  pilot power is used instead, and the result saturates at 8.8 Hz. The rows
  are 0.11 s apart, so a spread over 4.5 Hz aliases: this says "fast", not
  how fast.
- **`dly`.** For two equal paths `tau` apart the correlation of carriers one
  and two apart is `cos(pi df tau)` and `cos(2 pi df tau)` times the phase
  slope's powers. The slope the FFT window's place in the prefix adds is
  taken from the burst's own lag-1 sum; the signal power `s` solves
  `s^2 + r2 s - 2 r1^2 = 0`, so no noise estimate is needed, and
  `tau = acos(r1 / s) / (pi df)`. Any channel reads as the two equal paths
  with the same decorrelation.
- **`evm`.** The squared distance of each equalised data symbol to the
  nearest constellation point at the carrier's estimated amplitude (the same
  quantity the noise profile's `nv_data` accumulates), over the symbols'
  power.
- **`evm_excess`.** `evm` over what the noise alone predicts. The noise
  variance is the smaller of the pilot residual across time (the profile's
  second difference) and the second difference across carriers after the
  row's own slope is removed; each is inflated by the selectivity of its own
  axis, so the smaller is the better bound. The noise's effect on a decided
  symbol is `v (1 + Pd / (2 Pp))`: its own variance, plus the error of the
  two-pilot reference (`v/4` on its phase scaled to the symbol, `v/4` on its
  amplitude).
- **`evm_resid`.** `evm` over that prediction plus the mode's transmitter
  floor (table in `freedv_api.c`: DATAC1 -12.9, DATAC3 -14.0, DATAC4 -22.5,
  DATAC17 -10.0 dB; QAM16C2 has no clipper).

## Method

- **Synthetic pilots** (`test_chanstats`): two equal Rayleigh paths with a
  Gaussian Doppler spectrum (48 sinusoids each), a common phase slope, white
  noise at the stated per-pilot SNR; 27 carriers, 22 pilot rows, 80 bursts a
  cell, seeded.
- **The modem** (`utils/chanstats_bench`): one burst per trial, a fresh DATAC1
  or DATAC3 transmitter and receiver, normalised to a peak of 4000, through
  AWGN or the Watterson preset (`chanutil`, as `rx_bench`), a persistent channel
  per row, decoded with the noise statistics on; the statistics are read when
  the packet finishes, decoded or not. Fading rows are set by the noise
  density `No` (-43 gives an SNR3k of 12.8 to 13.1 on DATAC1, printed per
  row), not the SNR. The rung threshold used by the rules is the one in
  `arq_protocol.h` (DATAC1 +3 dB, DATAC3 -1 dB).
- Cells are the median and the 10 % / 90 % points over the packets that
  finished. "pkts" is how many of the trials reached a result at all.

## Results

### The estimators on synthetic pilots (80 bursts a cell)

Doppler spread, median [10 % .. 90 %] in Hz, delay 1.0 ms:

| pilot SNR | 0.1 Hz | 0.5 | 1.0 | 2.0 | 5.0 | 10.0 |
|---|---|---|---|---|---|---|
| 5 dB | 0.51 [0.20..2.83] | 0.52 [0.29..0.84] | 0.95 [0.64..1.19] | 3.43 [1.41..3.87] | 6.82 [5.56..8.78] | 8.78 [6.50..8.78] |
| 10 dB | 0.43 [0.15..2.04] | 0.52 [0.30..0.73] | 0.97 [0.74..1.15] | 1.97 [1.58..3.37] | 6.23 [5.06..8.78] | 8.78 [6.17..8.78] |
| 20 dB | 0.42 [0.16..1.58] | 0.50 [0.29..0.78] | 1.00 [0.69..1.23] | 1.94 [1.40..2.96] | 5.76 [4.67..8.50] | 7.80 [5.78..8.78] |

Delay (the two paths' separation), median [10 % .. 90 %] in ms, pilot SNR 10 dB:

| Doppler | 0 | 0.5 | 1.0 | 2.0 | 3.0 | 7.0 |
|---|---|---|---|---|---|---|
| 1 Hz | 0.00 [0.00..0.28] | 0.47 [0.00..0.74] | 0.93 [0.81..1.06] | 1.94 [1.68..2.01] | 2.82 [2.42..2.99] | 3.13 [0.00..6.82] |
| 10 Hz | 0.14 [0.00..0.40] | 0.52 [0.29..0.65] | 0.98 [0.90..1.06] | 1.98 [1.92..2.01] | 2.95 [2.77..3.00] | 5.53 [0.00..6.96] |

(The 5 and 20 dB rows are in the test's output.) Reading it:

- Doppler: 1 and 2 Hz are read to within about 5 %, 0.1 and 0.5 Hz are not
  told apart (both read 0.4-0.5: a 2.4 s burst cannot resolve a spread under
  about 0.5 Hz, and the estimator's floor is there), 5 Hz and 10 Hz read 6 to
  9 (the rows alias). Noise biases the faster cases up (2 Hz reads 3.4 at a
  pilot SNR of 5 dB).
- Delay: up to 3 ms, to within 5 %. At 7 ms the median is 3 to 6.6 and the
  10th percentile is 0: with one realisation of a slowly fading two-path
  channel in a burst (a handful of independent fades), the estimate is a
  property of that realisation. This is the estimator's limit, before the
  receiver has any say.

### On the modem, AWGN (DATAC1, 40 trials a row, all decoded)

| SNR3k | modem snr | dopp Hz | dly ms | evm dB | excess dB | resid dB | class |
|---|---|---|---|---|---|---|---|
| +5 | 4.1 | 0.24 | 0.33 | -6.06 | +0.38 | -0.51 | noise 100 % |
| +10 | 8.6 | 0.12 | 0.31 | -8.93 | +2.09 | -0.08 | clean 100 % |
| +15 | 11.8 | 0.09 | 0.30 | -11.11 | +4.70 | -0.01 | clean 100 % |
| +20 | 13.3 | 0.07 | 0.31 | -12.20 | +8.52 | +0.03 | clean 100 % |
| +25 | 14.1 | 0.04 | 0.31 | -12.63 | +13.00 | +0.04 | clean 100 % |
| +30 | 14.3 | 0.04 | 0.31 | -12.82 | +17.65 | +0.01 | clean 100 % |

(Medians. The 10-90 % range of `resid` is within 0.3 dB of the median at
every row; `dopp`'s 90th percentile is 1.3-2.2 Hz because now and then a
timing step of the receiver breaks the phase slope the sums assume: the
threshold of the Doppler rule, 2 Hz, is above it.)

The clipper is the floor: the same, with the transmitter's clipper off
(`CHANSTATS_TXCLIP=0`, 10 trials a row):

| SNR3k | evm dB | excess dB |
|---|---|---|
| +10 | -11.02 | +0.10 |
| +20 | -20.79 | +0.01 |
| +30 | -30.22 | +0.36 |

and QAM16C2 (no clipper): excess 0.00 at +20 and 0.51 at +30, -1.5 at +10
(decision errors make the measurement read low near the cliff, on every mode:
-1.2 dB at DATAC1's 0 dB).

The transmitter floor of each mode, from AWGN at 40 dB, 6 bursts:
DATAC1 -12.9, DATAC3 -14.0, DATAC4 -22.5, DATAC17 -10.0, QAM16C2 -34.7
(not a clipper's: the bench's own limit), DATAC14 -15.1, DATAC0 -14.0 dB.
Only the first five are in the table the code reads. DATAC1's own SNR
estimate saturates at 14 dB for the same reason.

### On the Watterson presets (DATAC1, SNR3k 12.8 to 13.1, 40 to 60 trials a row)

| preset (true) | pkts | decoded | modem snr | dopp Hz | dly ms | resid dB | class |
|---|---|---|---|---|---|---|---|
| ccir-good (0.5 ms, 0.1 Hz), 40 | 39 | 38 | 9.9 | 0.43 [0.19..1.53] | 0.56 [0.00..0.85] | -0.23 | clean 87 %, noise 13 % |
| ccir-moderate (1 ms, 0.5 Hz), 40 | 40 | 40 | 9.8 | 0.56 [0.34..0.74] | 1.01 [0.87..1.13] | -0.18 | clean 100 % |
| ccir-poor (2 ms, 1 Hz), 60 | 60 | 60 | 9.2 | 1.07 [0.92..1.35] | 1.97 [1.81..2.02] | -0.50 | clean 100 % |
| itur-hm (3 ms, 10 Hz), 40 | 12 | 0 | 0.1 | 8.78 [7.17..8.78] | 2.97 [2.82..3.01] | +4.57 | isi 25 %, doppler 75 % |
| ccir-flutter (0.5 ms, 10 Hz), 60 | 27 | 0 | -0.1 | 8.49 [6.52..8.78] | 0.38 [0.00..0.54] | +4.50 | doppler 100 % |
| itur-ld (6 ms, 10 Hz), 40 | 13 | 0 | -0.5 | 8.78 [7.08..8.78] | 4.08 [2.02..5.94] | -1.51 | noise 100 % |
| itur-mdv (7 ms, 1 Hz), 60 | 58 | 23 | 2.3 | 1.24 [0.92..1.81] | 0.00 [0.00..6.59] | -0.17 [-3.3..0.8] | noise 100 % |

`dopp` and `dly` follow the presets from 1 ms to 3 ms and from 1 Hz up. The
two ends do not resolve: 0.1 and 0.5 Hz read 0.4 to 0.6, and 7 ms (and,
less, 6 ms) is lost. Under fast fading the modem's SNR estimate is far below
the real SNR3k (-0.1 against 13), and most bursts of the 10 Hz presets never
reach a result (12-27 of 40-60): the statistics describe the bursts that
did.

### The classification, DATAC1, 10 dB over the rung (60 trials a row, the share of packets by class)

| condition | SNR3k | pkts | clean | narrowband | isi | doppler | txdist | noise |
|---|---|---|---|---|---|---|---|---|
| AWGN | 13 | 60 | **100** | | | | | |
| AWGN | 20 | 60 | **100** | | | | | |
| tone 1650 Hz, -10 dB | 13 | 60 | | **100** | | | | |
| ccir-poor | 12.8 | 60 | 100 | | | | | |
| ccir-flutter | 13.1 | 27 | | | | **100** | | |
| itur-mdv (expect isi) | 12.8 | 58 | | | **0** | | | 100 |
| clipper gain 5.0 (stock 2.7) | 13 | 60 | 3 | | | | **97** | |
| clipper gain 5.0 | 20 | 60 | | | | | **100** | |
| impulses | | | not built | | | | | |

DATAC3 at 10 dB over its threshold (SNR3k 9, 40 trials a row): AWGN clean
100 %, tone 1560 Hz -10 dB narrowband 100 %, ccir-poor clean 100 %,
ccir-flutter doppler 100 % of the 4 bursts that reached a result,
itur-mdv noise 100 % (of 38).

The proposal's acceptance (80 % of frames correct at 10 dB over the rung, on
six conditions) is met for four of its six (AWGN, tone, flutter, a clipped
transmitter), not for the disturbed NVIS channel (itur-mdv), and impulses
cannot be tested yet.

### Calibration

Thresholds are the proposal's, with two changes made on the bench:

- "Flat" is `dly` under **1 ms**, not 0.5: DATAC1 reads 0.3 on AWGN and 0.66
  with the clipper driven at 5.0.
- Transmitter distortion is judged on the **SNR the pilots' noise implies**
  (`-evm_noise_db` over 12), not on the modem's SNR estimate. That estimate
  is made from the errors being explained: with the clipper at 5.0 it falls
  from 25 dB to 9.9 dB, below the 12 dB of the rule, which could never have
  fired. Before the change: clean 100 % at the same cells.
- Rules 3 to 5 read `evm_resid`, not `evm_excess` (above). A harder clipper
  than stock is needed to fire the rule: at 3.5 the residual is 1.6 dB at 13
  and 2.7 at 25 dB (under the 3 dB threshold): called clean.

The narrowband rule (`nv_car_max - nv_car_med > 6 dB`) fired on none of the
AWGN, fading or clipped frames above (the 100 % rows), and on every tone
frame.

## Caveats

- Bench only, DATAC1 and DATAC3 mostly; DATAC4, DATAC17 and QAM16C2 have
  only the transmitter floor and the AWGN rows. No on-air data.
- **The disturbed NVIS case is not detected**, by any of the new fields.
  Probably the same cause as handbook pitfall 13: the receiver's window
  follows whichever path it found, and the burst is a mix of ISI and path
  changes that look like noise to every estimator that reads the pilots. A
  per-frame view (the window moving, the slope of the pilot phase jumping by
  the delay) might see it; not tried.
- `dly` is the separation of two equal paths: a channel of other shape reads
  as the one with the same decorrelation. With one fading realisation in a
  burst the estimate at 7 ms is a coin flip; a session's median over many
  bursts would do better, not measured.
- `dopp` cannot tell 0.1 from 0.5 Hz or 5 from 10 Hz. Noise biases it up.
- On the fast-fading presets most bursts never produce a result, so the
  statistics are the survivors'.
- The transmitter floors are measured with the bench's own transmitter
  settings. A different peer (another version, or a different radio) has a
  different floor. A peer whose clipper is much harder than ours is the case
  the rule exists for, and the only one tested is the clipper driven by the
  bench.
- The blanker's fraction, the silent-gap statistics (`gap_*`), the per-round
  line, the status snapshot and the `onair_logs.py` summary are not built:
  the "impulsive" rule and the gap half of the "narrowband" rule are skipped.
- No measurement of CPU: off, nothing runs; on, a few hundred multiplies per
  modem frame.
- The classification is reported, not used. Nothing here shows it helps a
  decision.

## Reproducing

```sh
make -C modem/freedv libfreedvdata.a -j2
make -C tests test_chanstats && ./tests/test_chanstats       # the estimators, the rules, AWGN, off
make -C utils chanstats_bench
./utils/chanstats_bench DATAC1 60 awgn 13 13 1                # a row; the columns are in the header
./utils/chanstats_bench DATAC1 60 awgn 13 13 1 1650 -10       # a tone at 1650 Hz, -10 dB
./utils/chanstats_bench DATAC1 60 ccir-poor -43 -43 1         # fading: the argument is No, SNR3k is printed
CHANSTATS_CLIPGAIN=5.0 ./utils/chanstats_bench DATAC1 60 awgn 20 20 1
CHANSTATS_TXCLIP=0 ./utils/chanstats_bench DATAC1 10 awgn 10 30 10
```

In a session: `MERCURY_CHANSTATS=1` (or `[main] chanstats = true`) and `-L file`;
the line per packet reads

```
chanstats: mode=10 ok=1 path=0 snr=9.9 foff=0.3 pcc=.. it=.. nvpk=2.1 nvhz=1650 nvflag=0 dopp=1.1 dly=1.97 evm=-10.0 evmx=2.2 evmr=-0.5 limit=clean
```
