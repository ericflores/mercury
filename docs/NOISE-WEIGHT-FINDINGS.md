# A carrier on the frequency: noise weights and the automatic notch

Issue #7; specification `docs/improvements/07-noise-aware-llr.md`.
Measured 2026-11-14 on the bench and in two-station sessions over a
simulated channel. Nothing here has been on the air.

## Question

A steady carrier inside the passband (a broadcast heterodyne, a birdie,
someone tuning up) takes OFDM frames with it at levels far below the
signal's. How much of that can the receiver undo alone, and with what?

## Answer

**Almost all of it, with the notch; the weights alone cover the easy half.**
DATAC1 at +5 dB, carrier level relative to the signal's power, frames
delivered of 60:

| carrier | stock | weights | notch |
|---|---|---|---|
| 1650 Hz (between two carriers), −3 dB | 0 | 59 | **60** |
| 1650 Hz, +3 dB | 0 | 53 | **60** |
| 1625 Hz (on a carrier's centre), −3 dB | 0 | 52 | **60** |
| 1625 Hz, +3 dB | 0 | 3 | **60** |
| 1650 Hz, +10 dB | 0 (none acquired) | 0 | **60** |
| 1650 Hz, +20 dB | 0 (none acquired) | 0 | **60** |

- A carrier 9 dB or more below the signal does no harm (60 of 60 stock at
  −9 and −15 dB).
- **Weights** (`MERCURY_NOISE_WEIGHT`): the receiver measures the noise on
  each carrier and, when a frame fails and some carriers stand out, decodes
  again with their soft bits scaled down. Enough when the carrier is weak or
  falls between two OFDM carriers.
- **Notch** (`MERCURY_NOTCH`): finds any line that stays on one frequency
  for a second, measures it to a few hundredths of a hertz and removes it
  from the audio before every decoder. It works at any level tried (to
  +20 dB), for every mode including the control frame and the MFSK floor,
  and never engaged on Mercury's own signals.
- Neither loses a frame when there is no carrier.

In a session (two stations, 600 bytes, SNR +20 dB): with a carrier 3 dB
over the signal the stock receiver takes 173 s and the notch 27-36 s; with
one 10 dB over it the stock receiver never connects and the notch finishes
in 24-34 s.

## Why weights are not enough

A carrier does two things before the soft bits exist:

1. **On a carrier's centre it captures the frequency tracker.** The tracker
   sums the pilots of all carriers; a tone concentrated on one of them
   dominates the sum, the estimate walks off (2 Hz measured) and every
   carrier rotates between pilot rows. The frame fails whatever weight the
   interfered carrier gets: 1625 Hz at +3 dB, 3 of 60.
2. **A few dB over the signal it stops acquisition:** at +10 dB nothing is
   acquired, so no decode attempt ever runs.

Hence the notch, in front of everything.

## What changed

- `modem/freedv/ofdm.c`: a per-carrier noise profile per burst, the larger
  of two measurements: the residual of each pilot against the mean of its
  two neighbours in time, and the mean squared distance of the data symbols
  to their nearest constellation point. The second was added after the
  first turned out blind: a tone whose phase advances by whole turns per
  modem frame (any multiple of 9.09 Hz for the 6 ms-prefix modes) cancels
  in the pilot residual. A tone at 2300 Hz (253 × 9.0909), 3 dB under the
  signal: 0 of 60 with the pilot measurement alone, 59 with both.
- `modem/freedv/freedv_700.c`: the weighted attempt (decode path 5). A
  carrier with more than twice the median carrier's noise has its symbols
  and amplitudes scaled by `sqrt(median / its noise)`. It runs only when a
  carrier stands out, on the best rows at hand (recycled, alternative
  estimate, stock), before the unweighted fallbacks, which still run if it
  fails. Same parity gate and CRC.
- `modem/rx_notch.{c,h}`: the notch. A Hann-windowed 1024-point spectrum
  every 64 ms; a line is a bin whose **minimum** power over the last second
  stands 10 dB over the mean of its neighbourhood (modulated signals and
  noise fluctuate from spectrum to spectrum, a carrier does not); its
  frequency from the bin's phase advance between spectra; a second-order
  notch 6 Hz wide. Up to four, each followed while it drifts and released
  2 s after it stops. With none engaged the audio passes bit for bit.
- `modem/modem.c`: the notch runs in the receive thread after the optional
  recording and before the blanker and the decoders.
- Switches, off by default: `MERCURY_NOISE_WEIGHT=1` / `[audio]
  rx_noise_weight`, `MERCURY_NOTCH=1` / `[audio] rx_notch`.
- Bench: `rx_bench` tokens `nw` and `nt`; `utils/rx_prefilter` and
  `utils/mfsk_interference_sweep.sh` for the MFSK floor;
  `utils/watterson_test --tone` and `MERCURY_CH_TONE` for sessions.

Tests: `tests/modem/test_noise_weight.c` (6), `tests/modem/test_rx_notch.c`
(6: carriers from −9 to +20 dB found within 1.14 s, to 0.01 Hz, 48-82 dB
down; two carriers; release; a 1 Hz/s drift followed; back-to-back bursts of
the control mode and every OFDM rung, and hopping tones, never engage it).

## Method

`utils/rx_bench`: one burst per trial, normalised to a peak of 4000, noise
and an unmodulated carrier added once, and that buffer decoded by every
variant. The carrier's level is relative to the burst's power. The notch
is given three seconds of the trial's noise and carrier before the buffer:
on the air a carrier has been there for a while when a burst arrives.
Delivered = CRC ok and the bytes that were sent. One seed.

## Results

### Other modes, carrier at the given level, per 60 (40 for DATAC17 and QAM16C2)

| mode, SNR3k | carrier | stock | weights | notch |
|---|---|---|---|---|
| DATAC16 (control, 3 carriers), −4 dB | 1500 Hz, 0 dB | 0 | 31 | **60** |
| DATAC16 | 1500 Hz, +10 dB | 0 | 0 | **60** |
| DATAC16 | 1530 Hz, +10 dB | 0 | 0 | **60** |
| DATAC15, −6 dB | 1500 Hz, 0 dB | 0 | 21 | **59** |
| DATAC15 | 1530 Hz, +10 dB | 0 | 0 | **60** |
| DATAC3, +2 dB | 1650 Hz, +3 dB | 0 | 48 | **60** |
| DATAC3 | 1625 Hz, +10 dB | 0 | 0 | **60** |
| DATAC17, +11 dB | 1650 Hz, +3 dB | 0 | 33 | **40** |
| DATAC17 | 1625 Hz, +10 dB | 0 | 0 | **40** |
| QAM16C2, +17 dB | 1650 Hz, 0 dB | 0 | 0 | **40** |
| QAM16C2 | 1625 Hz, +10 dB | 0 | 0 | **40** |
| DATAC1, 700 Hz (band edge), +5 dB | +10 dB | 0 | 0 | **60** |

Weights alone on other modes (first sweep, per 60 / 40): DATAC17 at −3 dB
0 → 39 of 40; QAM16C2 at −9 dB 0 → 40, at −3 dB 0 → 2 alone and 32 of 32
acquired with proposals 09 and 19 as well; DATAC4 at −3 dB 2 → 45; DATAC15
at −3 dB 4 → 32. With three or four carriers the weights can only give up
a third or a quarter of the frame, which is where they stop.

### Fading

DATAC1 per 60:

| channel | carrier | stock | weights | notch |
|---|---|---|---|---|
| CCIR Poor, +7.8 dB | 1650 Hz, −9 dB | 40 | 56 | not run |
| CCIR Poor | 1650 Hz, −3 dB | 0 | 42 (46 with 09 + 19) | not run |
| CCIR Poor | 1650 Hz, +3 dB | 0 | 9 (18) | **59** |
| CCIR Poor | 1625 Hz, +10 dB | 0 | 0 | **58** |
| disturbed NVIS, +11.8 dB | 1650 Hz, +3 dB | 0 | not run alone | 11; **57** with 09 + 19 + 26 and the weights |

### The MFSK floor (`utils/mfsk_interference_sweep.sh`, per 30)

| mode, SNR3k | carrier | as received | after the notch |
|---|---|---|---|
| floor (100), −5 dB | 1500 Hz, −3 dB | 30 | 30 |
| floor, −5 dB | 1500 Hz, +3 dB | 0 | **30** |
| floor, −5 dB | 1500 Hz, +10 dB | 0 | **30** |
| floor, −5 dB | 1320 Hz, +10 dB | 0 | **30** |
| floor, −9 dB | 1500 Hz, +3 dB | 0 | **30** |

### Neutrality: no carrier

Every cell equal, with and without (per 100 unless said):

| | stock | weights | notch |
|---|---|---|---|
| DATAC1 white noise +0.5 / +1.0 dB | 0 / 78 | 0 / 78 | 0 / 78 |
| DATAC3 white noise −4.5 / −4.0 dB | 12 / 76 | 12 / 76 | not run |
| DATAC16 white noise −10 / −9 dB | 83 / 98 | not run | 83 / 98 |
| DATAC15 white noise −12 / −11 dB | 1 / 51 | 1 / 51 | not run |
| QAM16C2 white noise +13.5 / +14.5 dB (per 60) | 60 / 60 | not run | 60 / 60 |
| DATAC1 CCIR Poor +3.9 / +1.9 dB | 70 / 14 | 70 / 14 | 70 / 14 |
| DATAC1 disturbed NVIS +7.8 / +5.8 dB (per 60) | 12 / 4 | 12 / 4 | 12 / 4 |
| QAM16C2 CCIR Poor +13.5 / +9.5 dB (per 40) | 33 / 9 | 33 / 9 | not run |

On recordings of real sessions (control frames, DATAC15, DATAC4, DATAC3 over
disturbed NVIS, `utils/rx_replay`) the notch engaged 0 times and the frames
decoded were the same.

### Sessions (two stations, lockstep transport, 600 bytes; `docs/INTEGRATION-AB-FINDINGS.md`)

| channel | stock | notch |
|---|---|---|
| SNR +20 dB, carrier 3 dB over the signal on 1650 Hz | 173 s, 173 s | 36 s, 27 s |
| SNR +20 dB, carrier 10 dB over the signal on 1625 Hz | no connection (2 of 2) | 24 s, 34 s |

## Cost

Not timed separately. The notch is one 1024-point FFT every 64 ms and up to
four second-order filters per sample. The weighted attempt is one more LDPC
decode, on a frame that failed while a carrier stood out.

## Caveats

- Bench and simulated sessions only; one seed; 40-100 trials a point.
- **Unmodulated carriers only.** An FSK or PSK signal, voice, or another
  data modem on part of the passband is not a line: the notch does not see
  it and only the weights apply. Not measured (the bench has no such
  interferer yet; proposal 16's phase 0 is about it).
- The notch needs about 1.1 s of a carrier before it engages. A carrier
  that appears during a burst costs that burst.
- It also removes a peer's ATU tuning carrier (TUNE), which nothing decodes.
- A drift of 1 Hz/s is followed (unit test); faster drift, chirps and
  sweepers were not tried and are not expected to be caught.
- With the notch on a carrier's frequency, the OFDM carrier under it loses
  a 6 Hz slice; no loss was measurable in these tables.
- False accepts: the weighted attempt is one more chance at the CRC behind
  the parity gate; the noise-only and wrong-session controls are in
  `docs/INTEGRATION-AB-FINDINGS.md`: none accepted.
- Not built: per-frame (time) weights, the gap noise profile,
  decode-and-subtract.

## Reproducing

```sh
make -C modem/freedv libfreedvdata.a && make -C utils rx_bench rx_prefilter mfsk_burst_file watterson_test
cd utils
./rx_bench DATAC1  60 5 5 1 awgn stock,nw,nt,nt+nw 1650 3
./rx_bench DATAC1  60 5 5 1 awgn stock,nw,nt,nt+nw 1625 10
./rx_bench DATAC16 60 -4 -4 1 awgn stock,nw,nt,nt+nw 1500 10
./rx_bench DATAC1  60 -38 -38 1 ccir-poor stock,nw,nt,nt+nw 1625 10
./rx_bench DATAC1 100 0.5 1.0 0.5 awgn stock,nt
./mfsk_interference_sweep.sh 100 30 -5 1500 10 0 0
make -C ../tests test_noise_weight test_rx_notch && ../tests/test_noise_weight && ../tests/test_rx_notch
```
