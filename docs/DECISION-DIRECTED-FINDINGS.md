# Decision-directed channel estimation: a last decode attempt

Issue #20; specification `docs/improvements/20-decision-directed.md`.
Measured 2026-11-14 with `utils/rx_bench`. Nothing here has been on the air.

## Question

When a frame fails, the LDPC decoder still leaves a belief about every bit.
Those beliefs turn the data symbols into soft pilots. Does re-estimating the
channel from them, and decoding again, deliver frames that the other
attempts (proposals 09 and 19) do not?

## Answer

**A little, in a band about half a dB wide below the cliff of 09 + 19, on
the long codes; next to nothing elsewhere.** It never lost a frame in any
cell, and it costs up to half as much CPU again on frames that fail anyway.

On top of 09 + 19, white noise:

| mode | level | 09 + 19 | 09 + 19 + this |
|---|---|---|---|
| DATAC1 (per 100) | −1.5 dB | 16 | **67** |
| DATAC1 | −1.0 dB | 89 | 93 |
| DATAC17 (per 60) | +0.5 dB | 7 | **25** |
| DATAC17 | +1.0 dB | 54 | 57 |
| QAM16C2 (per 60) | +6.5 dB | 55 | 60 |
| DATAC15 (per 100) | −12.0 dB | 19 | 24 |
| DATAC3 (per 100) | −5.0 / −4.5 dB | 35 / 77 | 35 / 77: none |

- One step lower (DATAC1 −2.0 dB, DATAC17 0 dB, QAM16C2 +5.5 dB) nothing
  decodes with or without it: the cliff moves by about 0.3 dB, it does not
  move by a decibel.
- Alone, without 09 and 19, it recovers part of what they recover (DATAC1
  at +0.5 dB: 2 → 75 against their 98; QAM16C2 at +7.5 dB: 0 → 24 against
  60). It is not a substitute for them.
- Under fading the gain on top of 09 + 19 is a few frames per hundred
  (CCIR Poor, DATAC1 at +1.9 dB: 58 → 64; at −0.1 dB: 15 → 22; DATAC17 at
  +8.1 dB: 56 → 60 of 60). On disturbed NVIS, on top of 09 + 19 + 26, it
  adds 1 frame in 240; on flutter nothing decodes at all, with or without.
- **Recommendation:** worth having on a desktop, as the last attempt; the
  first candidate to drop on a small machine (proposal 23's budget). It is
  part of `MERCURY_RX_ALL`.

## What changed

- `modem/freedv/mpdecode_core.c`, `interldpc.c`: the decoder can return the
  posterior of every code bit (`run_ldpc_decoder_post`,
  `ldpc_decode_frame_post`).
- `modem/freedv/freedv_700.c`: decode path 6. After every other attempt has
  failed, the posteriors of the attempt that came closest (fewest failed
  parity checks) are turned into soft symbols; the channel of each carrier
  is re-estimated as `h = (W·a + Σ y·conj(s)) / (W + Σ|s|²)`, where `a` is
  the estimate that attempt used and `W` its weight (2.0 on stock rows, 3.2
  on the alternative estimate), smoothed 1-2-1 across carriers in modes
  with nine carriers or more; the rows are equalised with it and decoded
  again. Two rounds at most. Same parity gate and CRC as every other
  attempt.
- Switch, off by default: `MERCURY_DD=1` / `[audio] rx_decision_directed`.
  With it off nothing is allocated and the decode is bit-identical
  (`tests/modem/test_decision_directed.c`, 4 tests).
- Bench: `rx_bench` token `dd`.

## Method

`utils/rx_bench`: one burst per trial, channel and noise applied once, the
same samples decoded by every variant. Delivered = CRC ok and the bytes
that were sent. 100 trials a point (60 for DATAC17, QAM16C2 and the NVIS,
flutter and moderate channels). One seed. "09 + 19" is `t4f3+cp4` (`t4+cp4`
for modes under nine carriers). CPU is the process time per trial with
`RX_BENCH_TIME=1`, x86-64, four benches running at once.

## Results

Cells are delivered frames: stock / this alone / 09 + 19 / 09 + 19 + this.

### White noise (SNR in 3 kHz)

| mode | dB | stock | alone | 09 + 19 | + this |
|---|---|---|---|---|---|
| DATAC1 /100 | −2.0 | 0 | 0 | 0 | 0 |
| | −1.5 | 0 | 0 | 16 | 67 |
| | −1.0 | 0 | 0 | 89 | 93 |
| | −0.5 | 0 | 0 | 97 | 97 |
| | 0.0 | 0 | 0 | 99 | 99 |
| | +0.5 | 2 | 75 | 98 | 98 |
| DATAC3 /100 | −5.5 | 0 | 0 | 9 | 9 |
| | −5.0 | 0 | 2 | 35 | 35 |
| | −4.5 | 5 | 12 | 77 | 77 |
| DATAC17 /60 | 0.0 | 0 | 0 | 0 | 0 |
| | +0.5 | 0 | 0 | 7 | 25 |
| | +1.0 | 0 | 0 | 54 | 57 |
| | +1.5 | 0 | 0 | 58 | 59 |
| | +3.0 | 35 | 45 | 60 | 60 |
| QAM16C2 /60 | +6.0 | 0 | 0 | 0 | 2 |
| | +6.5 | 0 | 0 | 55 | 60 |
| | +7.5 | 0 | 24 | 60 | 60 |
| | +8.0 | 38 | 60 | 60 | 60 |
| DATAC15 /100 | −13.0 | 0 | 0 | 1 | 2 |
| | −12.5 | 1 | 1 | 2 | 7 |
| | −12.0 | 1 | 4 | 19 | 24 |
| | −11.5 | 10 | 36 | 56 | 60 |
| DATAC4 /100 | −10.5 | 1 | 1 | 17 | 19 |
| | −10.0 | 4 | 10 | 39 | 39 |
| | −9.5 | 40 | 46 | 68 | 68 |
| DATAC16 /100 | −12.0 | 37 | 41 | 42 | 43 |
| | −11.5 to −10.0 | 60, 85, 85, 90 | 60, 85, 85, 90 | 61, 85, 86, 90 | 61, 85, 86, 91 |

The narrow modes (DATAC3, DATAC4, DATAC15, DATAC16) are limited by
acquisition at these levels (DATAC3 at −5 dB acquires 37 of 100), which no
decode attempt changes.

### Fading (measured SNR in 3 kHz)

| channel, mode | dB | stock | alone | 09 + 19 | + this |
|---|---|---|---|---|---|
| CCIR Poor, DATAC1 /100 | +5.9 | 95 | 95 | 95 | 96 |
| | +3.9 | 67 | 77 | 88 | 93 |
| | +1.9 | 16 | 24 | 58 | 64 |
| | −0.1 | 0 | 0 | 15 | 22 |
| CCIR Poor, DATAC3 /100 | +6.3 to +12.3 | 98-100 | same | same | same |
| CCIR Poor, DATAC17 /60 | +10.1 | 58 | 60 | 58 | 60 |
| | +8.1 | 55 | 58 | 56 | 60 |
| CCIR Poor, QAM16C2 /60 | +13.4 | 50 | 50 | 51 | 51 |
| | +11.4 | 38 | 42 | 43 | 44 |
| | +9.5 | 15 | 15 | 23 | 23 |
| CCIR Moderate, DATAC1 /60 | +3.9 | 41 | 47 | 54 | 55 |
| | +1.8 | 13 | 27 | 40 | 42 |
| CCIR Flutter, DATAC1 /60 | +4 to +8 | 0 | 0 | 0 | 0 |

Disturbed NVIS, stock / 09 + 19 + 26 / 09 + 19 + 26 + this, per 60:

| mode | dB | stock | 09 + 19 + 26 | + this |
|---|---|---|---|---|
| DATAC1 | +9.8 | 10 | 56 | 56 |
| | +7.8 | 9 | 54 | 54 |
| | +5.8 | 6 | 51 | 52 |
| | +3.8 | 0 | 32 | 32 |
| DATAC3 | +8.4 to +12.4 | 26-31 | 52-54 | 52-54 |

On three recordings of sessions over that channel (`utils/rx_replay`,
`docs/INTEGRATION-AB-FINDINGS.md`) it added one frame to one recording and
lost none.

### Cost: CPU seconds per trial

| mode, level | stock | alone | 09 + 19 | 09 + 19 + this |
|---|---|---|---|---|
| DATAC1, −2.0 dB (every frame fails) | 0.19 | 0.38 | 0.37 | 0.57 |
| DATAC1, 0.0 dB | 0.20 | 0.41 | 0.24 | 0.24 |
| DATAC17, 0.0 dB (every frame fails) | 0.38 | 0.87 | 0.86 | 1.34 |
| QAM16C2, +5.5 dB (every frame fails) | 0.30 | 0.80 | 0.82 | 1.29 |
| QAM16C2, +8.5 dB (every frame decodes) | 0.19 | 0.19 | 0.20 | 0.20 |

A frame that decodes costs nothing more. A frame that nothing decodes costs
about half as much again as with 09 + 19 (three times the stock receiver).
These figures are without early termination (proposal 23), which cuts the
cost of failed decodes on the long codes (`docs/RX-EFFORT-FINDINGS.md`);
the two together were not timed.

## Caveats

- Bench only, one seed, 60 to 100 trials a point: differences of two or
  three frames (most of the fading table) are inside the noise of the
  count. The cells that stand clear are DATAC1 at −1.5 dB and DATAC17 at
  +0.5 dB on white noise.
- The specification asked for a comparison with a genie (the channel known
  exactly) to say how much is left; it was not built.
- No frequency or sample-clock offset in these runs.
- CPU was measured on x86-64 only; nothing on a Raspberry Pi.
- False accepts: one more chance at the CRC behind the parity gate; the
  noise-only and wrong-session controls with every switch on are in
  `docs/INTEGRATION-AB-FINDINGS.md`.
- Not built: time-varying (per-row) re-estimation, which is what flutter
  would need; more than two rounds.

## Reproducing

```sh
make -C modem/freedv libfreedvdata.a && make -C utils rx_bench
cd utils
RX_BENCH_TIME=1 ./rx_bench DATAC1  100 -2.5 0.5 0.5 awgn stock,dd,t4f3+cp4,t4f3+cp4+dd
RX_BENCH_TIME=1 ./rx_bench DATAC17  60  0.0 3.0 0.5 awgn stock,dd,t4f3+cp4,t4f3+cp4+dd
RX_BENCH_TIME=1 ./rx_bench QAM16C2  60  5.5 8.5 0.5 awgn stock,dd,t4f3+cp4,t4f3+cp4+dd
./rx_bench DATAC1 100 -36 -28 2 ccir-poor stock,dd,t4f3+cp4,t4f3+cp4+dd
./rx_bench DATAC1  60 -40 -34 2 itur-mdv stock,t4f3+cp4+pt,t4f3+cp4+pt+dd
make -C ../tests test_decision_directed && ../tests/test_decision_directed
```
