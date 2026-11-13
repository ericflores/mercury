# Placing and tracking the FFT window from the pilots' delay profile

Issue #26; specification `docs/improvements/26-window-placement.md`.
Measured 2026-11-13 on the bench only. Nothing here has been on the air.

## Question

On a channel whose delay spread is near or beyond the cyclic prefix the
receiver delivers a small fraction of its frames whatever the SNR. Is that
the channel, or is it where the receiver puts its FFT window and how it
follows the timing? And can a failed burst be recovered from the samples
the receiver already holds, without touching what decodes today?

## Answer

**It is the receiver, and yes.** On ITU-R F.1487 mid-latitude disturbed NVIS
(two paths 7 ms apart, 1 Hz, against a 6 ms prefix):

| mode | SNR3k | stock | with the attempt |
|---|---|---|---|
| DATAC1 | +7.8 dB | 12 of 60 | **55 of 60** |
| DATAC1 | +13.9 dB | 8 of 40 | **40 of 40** |
| DATAC3 | +8.4 dB | 27 of 60 | **53 of 60** |
| DATAC17 | +16.0 dB | 2 of 40 | **40 of 40** |

A burst whose every other decode attempt failed is demodulated a second
time, by a chain that finds the paths in the pilot rows, remembers them
while they fade, and puts the FFT window and the frame grid where they
leave the least of another symbol inside the window. The stock attempt runs
first and unchanged, so no cell below shows fewer frames with the attempt
than without.

**It does not close the whole gap the lab receiver closed** (58 of 60 for
DATAC1, 57 for DATAC3): a burst on which the stock chain drops sync at the
unique word never reaches the attempt. See "Against the lab receiver".

**On channels where the window was never the problem it is proposal 09's
`t4` under another name**: the attempt decodes from the four-pilot-row
estimate, and on white noise and CCIR Poor its gain over stock is that
estimate's, not the tracker's.

## What changed

- `modem/freedv/ofdm.c`:
  - `ofdm_profile_paths()`: matching pursuit on the pilot rows of a modem
    frame. Every lag from −64 to 63 samples is a hypothesis "one path, this
    far from the window"; the one that explains most of the rows' energy is
    taken and removed, up to three times. A lag is a path if it explains
    four times what a noise lag would and a tenth of the strongest.
  - `ofdm_profile_place()`: tracks of the paths across frames (a faded path
    keeps its track for 25 frames, and the track is how the 16 ms ambiguity
    of a lag is resolved), and the window shift that minimises the samples
    of another symbol inside the window, weighted by the tracks' powers.
  - `ofdm_profile_replay()`: on a scratch copy of the demodulator's state as
    it was on the burst's first frame, with its own output buffers and the
    sample buffer shared and never written, demodulate the packet's `np`
    frames again. The first frame runs the stock timing update; from then on
    the tracker sets `sample_point` and `timing_est`, so the existing
    quarter-symbol grid shift follows the tracker's window and not the
    correlator. A frame whose rows would read past the end of the buffer is
    erased whole.
  - The live demodulator counts the samples shifted into its buffer and
    saves its state on a burst's first frame; nothing else in it changes.
- `modem/freedv/freedv_700.c`: one more attempt, after stock, the HARQ
  combine, 09 and 19, and only if all of them failed: the replay's symbols
  (from the `t4` estimate, with the replay chain's own amplitude scale)
  through the same `decode_packet_from_syms()`, parity gate and CRC.
  `freedv_get_rx_decode_path()` returns 4.
- Off by default. `MERCURY_PROFILE_TRACK=1` or `[audio] rx_profile_track =
  true`; modes with fewer than 9 carriers ignore it.
- `utils/rx_bench`: token `pt`.

`tests/modem/test_ofdm_profile.c` (14 tests) asserts, among others: off by
default and refused by a 3-carrier mode; a clean burst shows one path at lag
−43 to −41 and no shift; noise alone shows a path in under 3 % of frames
(measured 0.8 %); a replay with the stock timing reproduces the live `t4`
symbols to 3·10⁻⁷; with the feature on, the live demodulator's tracking
fields, the sample buffer and the stock, 09 and 19 symbol buffers are
bit-identical, call by call, to a receiver with it off; a strong signal
never reaches the attempt; on two paths 56 samples apart whose powers swap,
stock delivers 0 of 10 and the attempt 10.

## Method

`utils/rx_bench`: one burst per trial, normalised to a peak of 4000, the
channel (`common/watterson.c` through `utils/chanutil`) and noise applied
once, and that buffer decoded by every variant. Acquired = full `RX_SYNC`;
delivered = CRC ok and the bytes that were sent. On a fading preset the axis
is the noise density and the SNR3k column is what the channel measured. One
seed. `pt` is stock plus the new attempt; `t4f3+cp4` is stock plus the
attempts of 09 and 19.

## Results

### Disturbed NVIS (ITU-R F.1487 mid-latitude disturbed, 7 ms, 1 Hz)

DATAC1 (27 carriers), per 60:

| SNR3k (measured) | acquired | stock | `pt` | `t4f3+cp4` | `t4f3+cp4+pt` |
|---|---|---|---|---|---|
| +7.8 | 59 | **12** | **55** | 12 | 55 |
| +5.8 | 59 | **4** | **48** | 5 | 48 |
| +3.8 | 59 | **0** | **30** | 1 | 30 |

DATAC1 at high SNR, per 40:

| SNR3k (measured) | acquired | stock | `pt` |
|---|---|---|---|
| +17.9 | 40 | **12** | **38** |
| +13.9 | 40 | **8** | **40** |

DATAC3 (9 carriers), per 60:

| SNR3k (measured) | acquired | stock | `pt` | `t4f3+cp4+pt` |
|---|---|---|---|---|
| +12.4 | 60 | **28** | **52** | 52 |
| +8.4 | 60 | **27** | **53** | 53 |

DATAC17 (27 carriers, the long QPSK frame), per 40:

| SNR3k (measured) | acquired | stock | `pt` |
|---|---|---|---|
| +16.0 | 40 | **2** | **40** |
| +12.0 | 40 | **3** | **37** |

Stock does not improve with SNR on this channel, and proposals 09 and 19
do nothing for it (`t4f3+cp4` beside `stock`): the limit is not noise.

### Where the window was never the problem

DATAC1, CCIR 520 Poor (2 ms, 1 Hz), per 100:

| SNR3k (measured) | acquired | stock | `t4` | `pt` | `t4f3+cp4` | `t4f3+cp4+pt` |
|---|---|---|---|---|---|---|
| +3.9 | 99 | 70 | 77 | 75 | 91 | 91 |
| +1.9 | 96 | 14 | 28 | 27 | 59 | 59 |

DATAC1, white noise, per 100:

| SNR3k | acquired | stock | `t4` | `pt` |
|---|---|---|---|---|
| +0.5 | 100 | 0 | 84 | 83 |
| +1.0 | 100 | 78 | 99 | 99 |

DATAC3, white noise, per 100:

| SNR3k | acquired | stock | `t4` | `pt` |
|---|---|---|---|---|
| −4.5 | 71 | 12 | 61 | 61 |
| −4.0 | 97 | 76 | 97 | 97 |

QAM16C2, CCIR 520 Poor, per 40:

| SNR3k (measured) | acquired | stock | `pt` |
|---|---|---|---|
| +13.5 | 34 | 33 | 34 |
| +9.5 | 28 | 9 | 11 |

In these tables `pt` follows `t4` to within a frame or two, and adds
nothing to `t4f3+cp4`. The attempt costs a decode there for nothing the
other attempts do not already give; a station that runs 09 gains nothing
from it off long-multipath channels, and loses nothing either.

## Against the lab receiver

The prototype (`docs/improvements/prototype/lab3`) ran the tracker as the
whole receiver, from the first frame. On the same seeds:

| | stock | lab | as built |
|---|---|---|---|
| DATAC1, +7.8 / +5.8 / +3.8 dB, of 60 | 12 / 4 / 0 | 58 / 52 / 34 | 55 / 48 / 30 |
| DATAC1, +17.9 / +13.9 dB, of 40 | 12 / 8 | 40 / 40 | 38 / 40 |
| DATAC3, +12.4 / +8.4 dB, of 60 | 28 / 27 | 57 / 57 | 52 / 53 |

The difference is the bursts on which the stock chain fails the unique word
a few frames into the packet. The state machine then returns to search and
zeroes the sample buffer, the packet never completes, and there is nothing
to replay. The lab's chain passed the unique word itself. In one traced run
(DATAC3 at +8.4 dB, 60 bursts) the attempt ran on 30 bursts and delivered
all 30; the bursts it did not rescue are the ones it never ran on. That
count comes from arithmetic over the run, not from tracing each burst.

Two starts for the replay were compared: from the state saved before the
first frame, and after the first frame's stock timing update, which is what
the lab did. They are within one frame of each other (DATAC3 51 and 53
against 52 and 53; DATAC1 55, 48 and 30 either way). The second is kept:
the burst search does not reset the timing state, so the saved state is
whatever the previous burst left, and after a burst the stock tracker lost
it can be tens of samples off, where the tracker's first unwrapping would
guess wrong.

## Cost

One 2.1 GHz Xeon core, nothing else running:

| | DATAC1 | DATAC3 |
|---|---|---|
| the replay (demodulating the packet again) | 9.5-10 ms | 3 ms |
| the attempt's decode when it converges | 6-8 ms | not measured |
| for scale: the stock call that completes a failed packet | about 120 ms | not measured |

So a rescued DATAC1 frame costs about 18 ms more than the failure it
replaces. An attempt that fails too costs the replay plus a full decode
(about 100 ms for DATAC1): the same as each of the other attempts, and for
the same budget of proposal 23. A Raspberry Pi has not been timed.

With the feature on, a burst that decodes at the first attempt pays a
struct copy on its first frame and a counter.

## Caveats

- Bench only, one seed, 40-100 trials a point.
- **One two-path preset.** A real NVIS path has more than two rays, of
  unequal and changing strength, and delays that move. The tracker handles
  up to three paths a frame and spreads under 8 ms; none of that has been
  tried against a recording.
- Not run: `ccir-moderate`, `itur-lm`, the 10 Hz presets; a carrier
  frequency offset; a sample-clock offset; sweeps of the two detection
  thresholds, the track age and the margin; QAM16C2 on long multipath (its
  prefix is 4 ms).
- False accepts: the attempt is one more chance at the CRC's 2⁻¹⁶ escape,
  behind the parity gate; the noise-only and wrong-session controls have
  not been run.
- Sanitizers: `test_ofdm_profile`, `test_ofdm_cprecycle` and `test_ofdm_chanest` pass
  under ASan and UBSan; the whole suite under them and under TSan is left to
  CI.
- Not implemented: the second opinion at the unique word that would close
  the gap to the lab; skipping the replay when it would reproduce the stock
  rows; feeding the measured profile to proposal 19's guard; running the
  tracker live.
- Only the first packet of a burst is replayed, and only in burst mode.
  Mercury sends one packet a burst.
- Ladder constants are untouched. Whether rungs the ladder now avoids on
  such a channel become usable is a question for the simulator.

## Reproducing

```sh
make -C modem/freedv libfreedvdata.a && make -C utils rx_bench
cd utils
./rx_bench DATAC1   60 -38 -34 2 itur-mdv  stock,pt,t4f3+cp4,t4f3+cp4+pt
./rx_bench DATAC1   40 -48 -44 4 itur-mdv  stock,pt
./rx_bench DATAC3   60 -42 -38 4 itur-mdv  stock,pt,t4f3+cp4+pt
./rx_bench DATAC17  40 -46 -42 4 itur-mdv  stock,pt
./rx_bench DATAC1  100 -34 -32 2 ccir-poor stock,t4,pt,t4f3+cp4,t4f3+cp4+pt
./rx_bench DATAC1  100 0.5 1.0 0.5 awgn    stock,t4,pt
./rx_bench DATAC3  100 -4.5 -4.0 0.5 awgn  stock,t4,pt
./rx_bench QAM16C2  40 -48 -44 4 ccir-poor stock,pt
make -C tests test_ofdm_profile && ./tests/test_ofdm_profile
```
