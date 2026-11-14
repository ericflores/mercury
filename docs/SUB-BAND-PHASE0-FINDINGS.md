# Is a sub-band move needed? Proposal 16, phase 0

Issue #16; specification `docs/improvements/16-sub-band.md`.
Measured 2026-11-14 on the bench only (AWGN, no radio). Nothing here has
been on the air, and nothing in `modem/`, `datalink_arq/` or the wire
format changed.

## Question

The narrow modes (DATAC15 and DATAC16 at 187 Hz, DATAC4 at 250 Hz, DATAC3 at
562 Hz) always sit at 1500 Hz. Proposal 16 would let the receiver move one
to a clean part of the passband, at the price of a wire change. Two
receiver-only tools now exist: per-carrier noise weights (`nw`, proposal 07)
and the automatic carrier notch (`nt`, `modem/rx_notch.c`). Do they already
cover a narrowband interferer on 1500 Hz, so that the move is not worth
building? And if not, for which modes and which interferers?

## Answer

**It depends on whether the interferer is steady or modulated.**

1. **A steady carrier (CW): no move is needed.** The notch alone delivers
   60 of 60 for all four modes with the carrier on the mode's centre at 0,
   +10 and +20 dB relative to the signal. Weights alone rescue only the
   0 dB case and only partly (26 to 34 of 60 for DATAC15, DATAC16 and DATAC4;
   60 for DATAC3), and nothing at +10 dB. The notch is the tool; `nw` adds
   nothing to it here.
2. **A modulated narrow interferer (2-FSK, 45 baud, 170 Hz shift) on top of
   the mode: no receiver-only tool helps, and a move does.** At +10 dB and
   above every receiver variant delivers 0 of 60 for every mode, `nt` and
   `nw` included. At 0 dB they deliver at most 8 of 60 (DATAC15), 6 of 60
   (DATAC4), 0 of 60 (DATAC3), except DATAC16, which stock already gets 45
   of 60 through. The same interferer left where it is while the mode sits
   at 1000 or at 2000 Hz: **60 of 60 for DATAC15, DATAC16 and DATAC4 at
   every level, with the stock receiver.**
3. **DATAC3 needs more than the move.** Moved to 1000 or 2000 Hz, it still
   delivers 0 of 60 against a CW or FSK interferer at +10 dB and above, 500
   Hz away, because acquisition fails (0 of 60 acquired). DATAC3 has no
   receive filter, unlike the three narrower modes. A plain band-pass in
   front of it (a bench probe, `bp`) brings the moved mode to 60 of 60 at 1000
   and at 2000 Hz; with the interferer in band (mode centred) a filter cannot
   help and only the move does. So for DATAC3 the sub-band would have to come with a
   receive filter (a receiver-only change that is worth having on its own
   wherever the interferer is out of band).
4. **How close is "away".** For DATAC15 with FSK at +20 dB the centre must
   be at least 350 Hz from the interferer (60 of 60 at 350 Hz, 1 of 60 at
   300); at +10 dB 300 Hz is enough (60 of 60 at 300, 15 of 60 at 250 and
   200 Hz). The spec's five slots 400 Hz apart are about right for the 187
   to 250 Hz modes; for DATAC3 (562 Hz wide) the three slots of 600 Hz are
   not enough on their own: see the caveats.
5. **Neutrality holds.** With no interferer the moved modes deliver like the
   centred ones, at +6 dB over threshold (60 of 60 everywhere) and at the
   cliff (table at the end, differences within the binomial noise of 60
   trials).

**What this does not tell us** is how often a modulated narrowband
interferer is what limits a real HF link. That is for the telemetry of
proposal 10 (the spec's own stop condition) and for the on-air trials. If it
is rare, the notch has already taken the common case (a carrier) and the
move is a large wire change for the rest. The bench says only that, when
such an interferer is there, nothing short of moving the mode (or, for DATAC3
and out of band, filtering) helps.

## Method

- `utils/rx_bench` (paired: every column decodes the same impaired samples)
  with two additions made for this study:
  - the trailing arguments `fsk_hz fsk_db`: a continuous-phase 2-FSK signal
    (45.45 baud, 170 Hz shift, the RTTY standard; random data) centred on
    `fsk_hz`, at `fsk_db` relative to the burst's total power, as the
    existing tone. Its data and phase come from a generator of its own: a
    tone-plus-impulse run is sample-identical to the old binary's
    (`diff` of two runs, 30 trials, empty). The notch's pre-roll gets the
    same signal continued backwards (3 s), as it does for the tone.
  - `RX_BENCH_CENTRE=Hz`: the transmitter and every receiver are opened
    through `freedv_open_advanced(FREEDV_MODE_DATA_CUSTOM)` with the mode's
    own `ofdm_init_mode()` configuration and only the two centres changed.
    **No change was needed inside `modem/freedv/`.** The mode name stays, so
    the receive filter and the state machine are the mode's. The carriers sit
    on the symbol-rate grid, so the centre lands on the nearest grid
    position. Opened at 1500 Hz it delivers the same as the plain open
    (DATAC15, 20 trials at three SNRs, identical).
  - `bp` (a probe, bench only): a 401-tap linear-phase band-pass, 100 Hz
    wider than the mode's occupied band, in front of the decoder.
- `utils/subband_phase0.sh MODE SNR centred|moved` runs one mode's whole
  grid; the raw outputs are reproduced in the tables.
- AWGN. Operating point: about +6 dB over each mode's 90 % delivery point on
  this bench (30 trials): DATAC15 -10 dB (so -4), DATAC16 -10 (so -4),
  DATAC4 -8 (so -2), DATAC3 -3 (so +3). The 90 % points are a little
  different from the ones in `docs/MODES.md` (other harness, other payload
  sizes); the operating points are what they are, and every control row
  delivers 60 of 60 there.
- Interferer: a CW tone, or the FSK, on **1500 Hz**, at 0, +10 and +20 dB
  relative to the burst's total power (the burst's average power over the
  burst, the noise excluded). Note this is the interferer's power against
  the whole signal's: in the 187 Hz band of DATAC15 the signal to
  interference ratio is that figure with the sign changed.
- 60 trials a point. A cell is delivered of 60 (delivered = CRC ok and the
  bytes sent); in brackets, how many reached full sync. At 60 trials a
  cell's standard error is up to 6 frames.

## Results

### Mode centred (1500 Hz): the receiver-only tools

Delivered of 60 (acquired of 60). `none` is the control without interferer.

| mode, SNR3k | interferer | stock | `nw` | `nt` | `nt+nw` |
|---|---|---|---|---|---|
| DATAC15, -4 dB | none | 60 (60) | 60 | 60 | 60 |
| | CW 0 dB | 0 (58) | 26 (58) | 60 (60) | 60 (60) |
| | CW +10 | 0 (4) | 0 (4) | 60 (60) | 60 (60) |
| | CW +20 | 0 (0) | 0 (0) | 60 (60) | 60 (60) |
| | FSK 0 dB | 8 (58) | 8 (58) | 8 (58) | 8 (58) |
| | FSK +10 | 0 (2) | 0 (2) | 0 (10) | 0 (10) |
| | FSK +20 | 0 (0) | 0 (0) | 0 (23) | 0 (23) |
| DATAC16, -4 dB | none | 60 (60) | 60 | 60 | 60 |
| | CW 0 dB | 0 (59) | 31 (59) | 60 (60) | 60 (60) |
| | CW +10 | 0 (5) | 0 (5) | 60 (60) | 60 (60) |
| | CW +20 | 0 (0) | 0 (0) | 60 (60) | 60 (60) |
| | FSK 0 dB | 45 (56) | 45 (56) | 45 (56) | 45 (56) |
| | FSK +10 | 0 (0) | 0 (0) | 0 (8) | 0 (8) |
| | FSK +20 | 0 (0) | 0 (0) | 0 (18) | 0 (18) |
| DATAC4, -2 dB | none | 60 (60) | 60 | 60 | 60 |
| | CW 0 dB | 0 (35) | 34 (35) | 60 (60) | 60 (60) |
| | CW +10 | 0 (0) | 0 (0) | 60 (60) | 60 (60) |
| | CW +20 | 0 (0) | 0 (0) | 60 (60) | 60 (60) |
| | FSK 0 dB | 0 (31) | 6 (31) | 0 (34) | 6 (34) |
| | FSK +10 | 0 (0) | 0 (0) | 0 (0) | 0 (0) |
| | FSK +20 | 0 (0) | 0 (0) | 0 (0) | 0 (0) |
| DATAC3, +3 dB | none | 60 (60) | 60 | 60 | 60 |
| | CW 0 dB | 0 (60) | 60 (60) | 60 (60) | 60 (60) |
| | CW +10 | 0 (0) | 0 (0) | 60 (60) | 60 (60) |
| | CW +20 | 0 (0) | 0 (0) | 60 (60) | 60 (60) |
| | FSK 0 dB | 0 (60) | 0 (60) | 0 (60) | 0 (60) |
| | FSK +10 | 0 (2) | 0 (2) | 0 (7) | 0 (7) |
| | FSK +20 | 0 (0) | 0 (0) | 0 (0) | 0 (0) |

Reading: against a CW the notch is the whole answer and `nw` adds nothing to
it. Against the FSK nothing helps beyond 0 dB, and at 0 dB almost nothing:
`nw` gets back 6 of 60 on DATAC4 and the notch none. (The FSK
keys 85 Hz either side of the centre at 45 baud, so there is no steady line
for the notch to find. Some acquired counts rise with `nt` at +10 and +20 dB,
without a single frame more; I did not look into why.)
Above 0 dB the damage is at acquisition (the acquired counts drop to 0 to 10
of 60) and weights, which work on the soft bits, cannot act.

### Mode moved, interferer left on 1500 Hz

Delivered of 60, stock receiver and `nt+nw`.

| mode | centre | no interferer | CW 0 / +10 / +20 dB | FSK 0 / +10 / +20 dB |
|---|---|---|---|---|
| DATAC15 | 1000 Hz | 60, 60 | 60, 60 in all three | 60, 60 in all three |
| DATAC15 | 2000 Hz | 60, 60 | 60, 60 in all three | 60, 60 in all three |
| DATAC16 | 1000 Hz | 60, 60 | 60, 60 in all three | 60, 60 in all three |
| DATAC16 | 2000 Hz | 60, 60 | 60, 60 in all three | 60, 60 in all three |
| DATAC4 | 1000 Hz | 60, 60 | 60, 60 in all three | 60, 60 in all three |
| DATAC4 | 2000 Hz | 60, 60 | 60, 60 in all three | 60, 60 in all three |

(each cell "stock, `nt+nw`"; all 84 cells are 60 of 60 with 60 acquired.)

DATAC3, with the two columns that mattered added. Stock / `nt+nw` / `bp` /
`nt+nw+bp`; the same at 1000 and at 2000 Hz.

| interferer | stock | `nt+nw` | `bp` | `nt+nw+bp` |
|---|---|---|---|---|
| none | 60 | 60 | 60 | 60 |
| CW 0 dB | 60 | 60 | 60 | 60 |
| CW +10 | **0 (0)** | 60 | 60 | 60 |
| CW +20 | **0 (0)** | 60 | 60 | 60 |
| FSK 0 dB | 60 | 60 | 60 | 60 |
| FSK +10 | **0 (0)** | **0** | 60 | 60 |
| FSK +20 | **0 (0)** | **0** | 60 | 60 |

DATAC3 is the only mode that fails here, and it fails at acquisition, 500
Hz away from the interferer and with the mode's own band clear. The three
narrower modes have a receive filter (`rx_bpf` in `ofdm.c`, only for the
datac4, 13, 14, 15 and 16 names); DATAC3 and DATAC1 do not. I did not
diagnose the mechanism inside the acquisition search (a likely candidate is
the energy normalisation of the preamble correlation, which an interferer
10 dB over the signal dominates); the probe shows that removing the
out-of-band interferer by filtering is enough.

### How far from the interferer the mode has to sit

DATAC15, -4 dB, FSK on 1500 Hz, delivered of 60 (stock / `nt+nw`; the two
agree), mode centre as given (the carriers snap to the grid, and 1250 and
1300 Hz gave the same result, so probably the same grid position):

| centre | distance | FSK +10 dB | FSK +20 dB |
|---|---|---|---|
| 1350 Hz | 150 Hz | not run | 0 |
| 1300 Hz | 200 Hz | 15 / 18 | 0 |
| 1250 Hz | 250 Hz | 15 / 18 | 0 |
| 1200 Hz | 300 Hz | 60 | 1 |
| 1150 Hz | 350 Hz | 60 | 60 |
| 1000 Hz | 500 Hz | 60 | 60 |

DATAC3 at +10 dB FSK, the filter absent, delivered 0 of 60 at 900, 800 and
700 Hz (stock and `nt+nw`), although the mode delivers 30 of 30 there with
no interferer: the acquisition failure above, not a lack of separation.

### Neutrality at the cliff

Stock, no interferer, delivered of 60. The centre moves the carriers across
the band and the radio's response is not modelled, so this is only about the
modem.

| mode, SNR3k | 1500 Hz | 1000 Hz | 2000 Hz |
|---|---|---|---|
| DATAC15 -11 | 28 | 24 | 32 |
| DATAC15 -10 | 53 | 48 | 49 |
| DATAC16 -11 | 35 | 40 | 44 |
| DATAC16 -10 | 52 | 56 | 55 |
| DATAC4 -9 | 41 | 52 | 41 |
| DATAC4 -8 | 58 | 55 | 55 |
| DATAC3 -4 | 54 | 48 | 48 |

All inside two standard errors of 60 trials (about 4 to 6 frames at 50 %)
except DATAC4 -9 dB at 1000 Hz, which is higher, and DATAC3 -4 dB, where both
moved centres are 6 below (1.5 standard errors on each; not significant, not
cleared either). No sign of a systematic loss from the move.

## Caveats

- **AWGN and a flat passband only.** A real radio has a passband that tilts
  and a group delay that varies at 700 or 2300 Hz (the spec's risk list). The
  moved columns say that the *modem* does not mind the centre; they say
  nothing about the radio. Fading was not tried: the interferer is the
  question here, and the fading presets slow every row.
- **One interferer, on the centre of the centred mode.** This is the worst
  case for the centred columns and an easy case for the moved ones: the
  interferer was 500 Hz from the moved mode's centre, hundreds of Hz clear of its band. A real interferer is not
  placed where the mode is, and a receiver would need to find a clean slot
  without knowing where the interferer is (the spec's gap spectrum, proposal
  10). The separation table above is the only measurement of how close is too
  close, and it is for one mode and one interferer shape.
- **One modulation.** 45.45 baud, 170 Hz shift 2-FSK at three levels. Other
  signals (PSK31, a data mode on the neighbouring frequency, a wide
  multi-tone) are not covered; PSK31 is narrower and would be nearer to the
  notch's reach, an OFDM neighbour wider and nearer the real problem of
  proposal 07's weights.
- **One SNR per mode (+6 dB over the 90 % point), 60 trials.** A cell of 0 or
  60 is clear; the cells in between (26, 31, 34, 45, 15, 18) carry an error of
  several frames, and the relative order of `nw` and stock at 0 dB CW is not
  worth more than that.
- **The level is relative to the burst's total power**, and the modes' narrow
  band holds all of it, so the in-band signal-to-interference ratio is the
  stated level reversed. A real interferer that is +20 dB over the signal
  only for the 3 kHz as a whole is a rarer thing than one that is +20 dB over
  a 187 Hz signal's.
- **`bp` is a probe, not a feature.** It is a 401-tap filter written for the
  bench, tuned to the nominal band of the mode; it has not been checked for
  false acquisition, timing, or CPU on a Pi. It says only that filtering is
  enough for an out-of-band interferer on DATAC3, not that this filter is
  what to ship.
- **Nothing on the false-accept side:** no decode attempt was added.
- **How often this happens on the air is unknown.** See the answer.

## What this means for the decision

1. Do not build a wire change for steady carriers: the notch (already in
   `MERCURY_NOTCH`) delivers them at +20 dB for every narrow mode.
2. The case for the sub-band is the modulated narrowband interferer, and the
   bench shows it is real in the modem sense: no receiver-only tool recovers
   a frame under it, and the move recovers all of them for DATAC15, DATAC16
   and DATAC4. Whether it is worth a negotiated wire change depends on how
   often such interference limits a session, which the telemetry of
   proposal 10 and an on-air survey (the IC-7300 and the SDR give the means)
   have to say. This phase-0 study does not provide that.
3. If it is built: the slot spacing of 400 Hz is about right for the 187 to
   250 Hz modes; DATAC3 needs a receive filter first (receiver-only, and
   useful on its own), and then its slots can be as the spec says.
4. A cheaper first step that this study suggests and has not measured: a
   receive filter for DATAC3 and DATAC1 alone fixes the "interferer out of
   band at +10 dB" failure with no wire change.

## Reproducing

```sh
make -C modem/freedv libfreedvdata.a -j2 && make -C utils rx_bench
cd utils
./subband_phase0.sh DATAC15 -4 centred      # about 4 min
./subband_phase0.sh DATAC15 -4 moved        # about 3 min
./subband_phase0.sh DATAC16 -4 centred; ./subband_phase0.sh DATAC16 -4 moved
./subband_phase0.sh DATAC4  -2 centred; ./subband_phase0.sh DATAC4  -2 moved
./subband_phase0.sh DATAC3  +3 centred
MOVED_VARIANTS=stock,nt+nw,bp,nt+nw+bp ./subband_phase0.sh DATAC3 3 moved
# separation (one row)
RX_BENCH_CENTRE=1200 ./rx_bench DATAC15 60 -4 -4 1 awgn stock,nt+nw 0 -99 0 0 1500 20
# neutrality at the cliff
RX_BENCH_CENTRE=1000 ./rx_bench DATAC15 60 -11 -10 1 awgn stock
# the interferer by hand: ... [tone_hz tone_db] [imp_rate imp_db] [fsk_hz fsk_db]
./rx_bench DATAC4 60 -2 -2 1 awgn stock,nt+nw 0 -99 0 0 1500 10
```

Each `subband_phase0.sh` call is under 6 minutes on one core; the
bench is deterministic (the same arguments give the same table).
