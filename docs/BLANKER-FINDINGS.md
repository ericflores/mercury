# Impulse noise: the blanker

Issue #8; specification `docs/improvements/08-impulse-blanker.md`.
Measured 2026-11-14 on the bench and in two-station sessions over a
simulated channel. Nothing here has been on the air, and the impulses are
synthetic.

## Question

Short, strong noise bursts (ignition, switching supplies, electric fences,
static clicks) spread over every carrier of the OFDM symbols they land in.
Does zeroing them before the decoders bring the frames back, and what does
it cost when there are none?

## Answer

**Yes for the wide modes, at a threshold of 8.** DATAC1 at +5 dB with 2 ms
noise bursts 25 dB over the signal, three a second: stock 0 of 60, blanker
60. The same for DATAC17 and QAM16C2. The narrow modes (DATAC3, DATAC15,
the control frame) and the MFSK floor survive these impulses without help.

**The threshold matters more than the specification expected.** 4 cuts the
signal's own peaks and loses frames at every cliff (DATAC3 at −4 dB: 76 →
36 of 100). 6 is neutral on white noise but costs the unclipped top rung a
few frames under fading. 8 removes the impulses as well as 6 or better and
is within the run-to-run noise everywhere without them. **Recommended: 8.**

In a session (600 bytes, SNR +25 dB, five 2 ms bursts a second 25 dB over
the signal): stock 275 s and 195 s, with the blanker 27 s both times.

## What changed

- `modem/rx_blanker.{c,h}`: a sample more than `k` times the tracked level
  (the mean of |x| over samples that were passed, 256 ms) is zeroed, with
  every sample that triggers after it and 1 ms after each. The audio is
  delayed 8 ms so that the decision about a sample is taken with what
  follows it in hand: if the lower quartile of the samples 4 to 8 ms later
  is still high, something that lasts has started (a burst arriving after
  silence, a long crash); nothing is zeroed and the level jumps. A cap
  suspends blanking for the rest of any second in which 5 % was blanked.
- `modem/modem.c`: in the receive thread, after the optional recording and
  the notch, before every decoder. Off, it is never called.
- `MERCURY_BLANKER=<k>` / `[audio] rx_blanker = <k>`; 0 (the default) is off.
- Bench: `rx_bench` token `bK`; `utils/rx_prefilter` for the MFSK floor;
  `utils/watterson_test --impulses` and `MERCURY_CH_IMPULSES` for sessions.

`tests/modem/test_rx_blanker.c` (6 tests): off is a pass-through; on is a
pure delay on steady noise; clean bursts of DATAC1, QAM16C2, DATAC15 and
DATAC16 after a second of silence, at full scale and 30 dB down, lose
nothing; impulses 25 dB up keep 0.06 % of their energy; the cap holds; the
level is not captured by the impulses.

One design error found by the test: judging "does it last" on the *mean*
of the lookahead took two impulses 6 ms apart for a burst, adopted their
level and let the next three impulses through. The lower quartile does not.

## Method

`utils/rx_bench`: one burst per trial, normalised to a peak of 4000; noise,
and noise bursts of 2 ms at random times whose rms is the given number of dB
over the burst's rms, added once; that buffer decoded by every variant, the
blanker run over a copy of it first (a fresh blanker per trial, with the
0.4 s of noise before the burst to set its level). Delivered = CRC ok and
the bytes that were sent. One seed.

## Results

### Impulses, white noise (acquired is 59-60 of 60 with the blanker)

DATAC1 at +5 dB, per 60:

| impulses | stock | 4 | 6 | 8 | 10 |
|---|---|---|---|---|---|
| 3 /s, +15 dB | 59 | 60 | 60 | 60 | 60 |
| 3 /s, +25 dB | 0 | 57 | 59 | **60** | 60 |
| 3 /s, +35 dB | 0 | 54 | 60 | **60** | not run |
| 10 /s, +25 dB | 0 | 37 | 50 | **55** | 57 |
| 30 /s, +25 dB | 0 | 0 | 0 | 0 | not run |

Thirty bursts a second is 6 % of the time: more than the cap lets through,
and nothing delivers either way.

Other modes, 3 /s at +25 dB:

| mode, SNR3k | stock | 4 | 6 | 8 | 10 |
|---|---|---|---|---|---|
| DATAC17, +11 dB (per 40) | 0 | 36 | 39 | **40** | not run |
| QAM16C2, +17 dB (per 40) | 0 | 36 | 40 | 39-40 | 40 |
| DATAC1, CCIR Poor +7.8 dB (per 60) | 0 | 57 | 58 | 57 | 58 |
| DATAC3, +2 dB (per 60) | 59 | 60 | 60 | 60 | not run |
| DATAC15, −6 dB (per 60) | 60 | 60 | 60 | 60 | not run |
| DATAC16, −4 dB (per 60) | 60 | 60 | 60 | 60 | not run |

With proposals 09 and 19 but no blanker DATAC1 delivers 7 of 59; with them
and the blanker 60 of 60: the decode attempts do not replace it.

### The MFSK floor (`utils/mfsk_interference_sweep.sh`, per 30, 5 bursts a second)

| mode, SNR3k | impulses | as received | after the blanker at 6 |
|---|---|---|---|
| floor (100), −5 dB | +25 dB | 30 | 30 |
| floor, −5 dB | +35 dB | 25 | **30** |
| floor, −9 dB | +25 dB | 30 | 30 |
| 500 Hz floor (103), −8 dB | +25 dB | 30 | 30 |

### Neutrality: no impulses

Per 100 unless said; `any` is the trials at least one column delivered.

| | stock | 4 | 6 | 8 |
|---|---|---|---|---|
| DATAC1 white noise +1.0 dB | 78 | **62** | 78 | 78 |
| DATAC1 +10 / +30 dB (per 40) | 40 / 40 | 40 / 40 | 40 / 40 | 40 / 40 |
| DATAC3 white noise −4.5 / −4.0 dB | 12 / 76 | **5 / 36** | 12 / 77 | 12 / 77 |
| DATAC15 white noise −12 / −11 dB | 1 / 51 | 0 / **38** | 1 / 51 | 1 / 51 |
| DATAC16 white noise −10 / −9 dB | 83 / 98 | 80 / 97 | 83 / 98 | 83 / 98 |
| DATAC17 white noise +7 / +8 dB (per 60) | 60 / 60 | 60 / 60 | 60 / 60 | 60 / 60 |
| QAM16C2 white noise +13.5 / +14.5 / +30 dB (per 60, 60, 40) | all | all | all | all |
| DATAC1 CCIR Poor +3.9 / +1.9 dB | 70 / 14 | 61 / 11 | 69 / 14 | 70 / 14 |

Under fading, a second run with 6, 8 and 10:

| | stock | 6 | 8 | 10 |
|---|---|---|---|---|
| QAM16C2 CCIR Poor +13.4 dB (per 100; acquired 88 / 85 / 86 / 83) | 84 | 80 | 82 | 79 |
| QAM16C2 CCIR Poor +11.4 dB (per 100) | 57 | **51** | 56 | 57 |
| DATAC17 CCIR Poor +14.1 / +12.1 dB (per 60) | 60 / 60 | 60 / 60 | 60 / 60 | 60 / 60 |
| DATAC1 disturbed NVIS +7.9 / +5.9 dB (per 100) | 24 / 10 | 23 / 10 | 24 / 10 | 24 / 10 |
| QAM16C2 white noise +20 dB (per 60) | 60 | 60 | 60 | 60 |

The blanker changes samples, so at a cliff the same trial can fall either
way; the counts, not the sets, are what agree (DATAC1 at +1.0 dB: 78 each,
80 delivered by at least one). QAM16C2 under CCIR Poor is where a threshold
of 6 shows: the mode is sent unclipped, its peaks reach 4.6 times its mean
level, and after a fade the level estimate is behind the signal.

On recordings of sessions over disturbed NVIS (`utils/rx_replay`) the
blanker at 6 removed 0.017-0.019 % of the samples and the frames decoded
were the same; in sessions it logs a few events every ten seconds
(0.01-0.04 %) on a channel with no impulses at all.

### Sessions (two stations, lockstep transport, 600 bytes; `docs/INTEGRATION-AB-FINDINGS.md`)

| channel | stock | blanker at 6 |
|---|---|---|
| SNR +25 dB, 5 bursts a second 25 dB over the signal | 275 s, 195 s | 27 s, 27 s |

## Caveats

- **Synthetic impulses**: 2 ms of Gaussian noise at random times, straight
  into the audio. Real static arrives shaped by the radio's filter and AGC,
  lasts longer, and a radio's AGC pumping on it does damage no blanker in
  the audio can undo. Use fast or no AGC for data. Recordings
  (`MERCURY_RX_DUMP`) replayed with `rx_replay ... stock,b8` are how to
  find out; the recording is taken before the blanker.
- An impulse longer than about 4 ms is taken for something that lasts and
  is not blanked.
- Bench and simulated sessions only; one seed; 40-100 trials a point. The
  sessions were run at 6, before the threshold comparison.
- The audio reaches the decoders 8 ms later when it is on.
- Not built: the periodic mode (mains-locked noise below the threshold),
  the map of blanked samples for the decoder, the soft variant.

## Reproducing

```sh
make -C modem/freedv libfreedvdata.a && make -C utils rx_bench rx_prefilter mfsk_burst_file watterson_test
cd utils
./rx_bench DATAC1  60 5 5 1 awgn stock,b4,b6,b8 0 -99 3 25
./rx_bench DATAC1  60 5 5 1 awgn stock,b6,b8,b10 0 -99 10 25
./rx_bench QAM16C2 40 17 17 1 awgn stock,b4,b6,b8 0 -99 3 25
./rx_bench DATAC3 100 -4.5 -4.0 0.5 awgn stock,b4,b6,b8
./rx_bench QAM16C2 100 -48 -46 2 ccir-poor stock,b6,b8,b10
G=0.02 ./mfsk_interference_sweep.sh 100 30 -5 1500 none 5 35
make -C ../tests test_rx_blanker && ../tests/test_rx_blanker
```
