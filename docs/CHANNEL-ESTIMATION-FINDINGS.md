# A second decode attempt from a channel estimate over more pilots

Issue #9; specification `docs/improvements/09-channel-estimation.md`.
Measured 2026-11-13 on the bench only. Nothing here has been on the air.

## Question

The OFDM data modes take each carrier's phase and amplitude reference from
the mean of two pilot symbols. How much does the noise in that reference
cost, and can it be recovered without risk to what works today?

## Answer

**About 1 dB at the AWGN cliff of every mode tested, and two to three times
the frames at DATAC1's cliff under CCIR Poor.** Recovered by decoding a
packet a second time, only when its stock decode has failed, from an
estimate averaged over four pilot rows and (on the modes with enough
carriers) adjacent carriers. Because the stock attempt runs first and
unchanged, no cell of any table below shows a frame lost.

**It does nothing on disturbed NVIS** (7 ms delay spread). That channel is
limited by multipath, not by noise in the reference, and the tables say so.

## What changed

- `modem/freedv/ofdm.c`, `ofdm_chanest_alt()`: per carrier, the previous,
  this, next and future pilot rows weighted 1,3,3,1 (the previous row left
  out of a burst's first modem frame, the future row out of its last), then
  optionally 1-2-1 (`t4f3`) or 1-2-3-2-1 (`t4f5`) across carriers after the
  common phase slope across carriers has been tracked and removed.
- `modem/freedv/freedv_700.c`: the symbols corrected by that estimate are
  kept beside the stock ones; if the stock decode and the HARQ combine both
  fail the CRC, one more LDPC decode runs on them, behind the same
  parity-check gate as the combine path.
- Off by default. `MERCURY_CHANEST=t4|t4f3|t4f5|auto`; `auto` is `t4` on the
  3- and 4-carrier modes and `t4f3` on the rest.
- `freedv_get_rx_decode_path()` says which attempt delivered a frame.

## Method

`utils/rx_bench`: one burst per trial, normalised to a peak of 4000, noise
(and fading, from `common/watterson.c` through `utils/chanutil`) applied
once, and that one buffer decoded by every variant. Acquired = full
`RX_SYNC`; delivered = CRC ok and the bytes that were sent. Cells are
delivered per trials; every variant acquires the same bursts, because
acquisition is untouched. On fading presets the axis is the noise density
and the SNR3k column is what the channel measured. One seed.

Calibration: the stock column reproduces the published benches where they
overlap (DATAC15 near 50 % at −11 dB and DATAC16's acquisition counts,
`docs/ACQ-TUNING-FINDINGS.md`; DATAC3's cliff between −4.8 and −3.8 dB,
`docs/HARQ-FINDINGS.md`).

## Results

### AWGN

DATAC1, per 100 (all acquired):

| SNR3k | stock | t4 | t4f3 | t4f5 |
|---|---|---|---|---|
| −1 | 0 | 0 | 0 | 7 |
| 0 | **0** | 11 | **95** | **96** |
| +1 | 87 | 97 | 98 | 98 |

DATAC3, per 100:

| SNR3k | acquired | stock | t4 | t4f3 | t4f5 |
|---|---|---|---|---|---|
| −6 | 7 | 0 | 0 | 0 | 0 |
| −5 | 38 | **0** | 3 | **24** | **28** |
| −4 | 96 | **59** | 81 | **93** | **93** |
| −3 | 100 | 99 | 99 | 100 | 100 |

DATAC15 (3 carriers), per 150:

| SNR3k | acquired | stock | t4 |
|---|---|---|---|
| −12 | 95 | 3 | 23 |
| −11 | 130 | 70 | 100 |
| −10 | 146 | 131 | 133 |

DATAC16, the control mode (3 carriers), per 150:

| SNR3k | acquired | stock | t4 |
|---|---|---|---|
| −13 | 38 | 5 | 17 |
| −12 | 88 | 59 | 71 |
| −11 | 123 | 109 | 110 |
| −10 | 147 | 138 | 138 |

DATAC15, DATAC16 and DATAC3 at their lowest rows are limited by acquisition
(38 of 150, 38 of 100): the second attempt converts more of the acquired
bursts, it does not acquire more of them. That is issue #3.

DATAC17, per 60 (all acquired):

| SNR3k | stock | t4 | t4f3 | t4f5 |
|---|---|---|---|---|
| +2 | **0** | 0 | **58** | **59** |
| +3 | 29 | 59 | 60 | 60 |
| +4 | 60 | 60 | 60 | 60 |

QAM16C2, per 60 (all acquired):

| SNR3k | stock | t4 | t4f3 | t4f5 |
|---|---|---|---|---|
| +7 | **0** | 0 | **59** | **60** |
| +8 | 37 | 59 | 60 | 60 |
| +9 | 60 | 60 | 60 | 60 |

### Fading

DATAC1, CCIR 520 Poor (2 ms, 1 Hz), per 100:

| SNR3k (measured) | acquired | stock | t4 | t4f3 | t4f5 |
|---|---|---|---|---|---|
| +7.9 | 99 | 98 | 98 | 98 | 98 |
| +5.9 | 99 | 96 | 96 | 97 | 97 |
| +3.9 | 99 | **70** | 76 | **87** | **88** |
| +1.9 | 96 | **17** | 27 | **47** | **51** |

DATAC1, ITU-R F.1487 mid-latitude disturbed NVIS (7 ms, 1 Hz), per 100:

| SNR3k (measured) | acquired | stock | t4 | t4f3 | t4f5 |
|---|---|---|---|---|---|
| +7.9 | 99 | 24 | 26 | 24 | 24 |
| +5.9 | 99 | 10 | 11 | 11 | 11 |
| +3.9 | 99 | 4 | 4 | 4 | 4 |
| +1.9 | 96 | 0 | 0 | 0 | 0 |

DATAC1, CCIR 520 Flutter (0.5 ms, 10 Hz): nothing delivered by any variant
from +2 to +8 dB (93-100 of 100 acquired). The mode does not work on that
channel with or without the second attempt.

So the gain is where the reference noise is the limit (AWGN, CCIR Poor) and
absent where the channel itself is (delay spread beyond the cyclic prefix,
or a Doppler spread the pilot spacing cannot follow). No loss in either.

## Readings

- The gain is the reference noise, as predicted: with N pilots averaged the
  effective SNR of a coherent symbol is lowered by about
  `1 + 1/N + 1/(N·γ)`. Smoothing across carriers is where most of it comes
  from on the modes that have carriers to smooth across; time averaging
  alone (`t4`) is worth much less on DATAC1 and DATAC3, and is all there is
  on the 3-carrier modes.
- `t4f5` is slightly ahead of `t4f3` on AWGN and under CCIR Poor. `auto`
  stays on `t4f3` until the presets with longer delay spread and a frequency
  offset have been run, because wider smoothing assumes a flatter channel.
- Nothing is lost anywhere: in every row "any variant delivered" equals the
  best single variant, and the unit test asserts frame by frame that a
  stock delivery is never missing with the feature on.

## Cost

One more LDPC decode on a packet that failed, which is the expensive case
(a failed decode runs all 100 iterations). Packets that decode first time
pay only the second estimate: four weighted sums per carrier per modem
frame and a copy of the symbol buffer. Not yet timed on a Pi 4.

## Caveats

- Bench only, one seed, 60-150 trials a point.
- No frequency offset in these runs. The slope tracker and the frequency
  tracker interact in principle; `rx_bench` needs the offset option before
  this is believed with a mistuned peer.
- `ccir-moderate`, the high-latitude presets, and every mode but DATAC1
  under fading, not yet run on the as-built code (the prototype's QAM16C2 fading rows gained
  a little and lost nothing: issue #9).
- False accepts: the second attempt is another chance at the 2^-16 CRC
  escape. It runs behind the parity gate, but the noise-only and
  wrong-session controls of the specification have not been run.
- The per-mode `EsNodB` LLR scale was not re-tuned for the second attempt.
- `ARQ_SNR_MIN_*` and the ladder are untouched. Whether rungs should be
  entered earlier is a ladder question for the sim, not answered here.

## Reproducing

```sh
make -C modem/freedv libfreedvdata.a && make -C utils rx_bench
cd utils
./rx_bench DATAC1  100  -1   2 1 awgn      stock,t4,t4f3,t4f5
./rx_bench DATAC3  100  -7  -3 1 awgn      stock,t4,t4f3,t4f5
./rx_bench DATAC15 150 -12 -10 1 awgn      stock,t4
./rx_bench DATAC16 150 -13 -10 1 awgn      stock,t4
./rx_bench DATAC17  60   2   5 1 awgn      stock,t4,t4f3,t4f5
./rx_bench QAM16C2  60   7   9 1 awgn      stock,t4,t4f3,t4f5
./rx_bench DATAC1  100 -38 -32 2 ccir-poor    stock,t4,t4f3,t4f5
./rx_bench DATAC1  100 -38 -32 2 itur-mdv     stock,t4,t4f3,t4f5
./rx_bench DATAC1  100 -38 -32 2 ccir-flutter stock,t4,t4f3,t4f5
make -C tests test_ofdm_chanest && ./tests/test_ofdm_chanest
```
