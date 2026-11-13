# 25 — A VHF/UHF profile (issue #25)

**Kind:** new modes and a second set of link timings, selected by
configuration; the new modes are new rungs, so negotiated. Nothing changes
for an HF station.
**Depends on:** 00-bench (it needs an FM channel model), 15 (the dense
constellations and high-rate codes are the same work), 23.
**Switch:** `[link] profile = hf | vhf-fm`, default `hf`.

## Goal

On a line-of-sight VHF/UHF link through ordinary FM transceivers, carry
several times what the HF ladder can, by not paying for an ionosphere that
is not there. And settle, with numbers, what spread spectrum would add.

## Facts

- The ladder was built for HF. Every OFDM data mode spends 6 ms of each
  22 ms symbol on the cyclic prefix (4 of 20 ms for QAM16C2) and one symbol
  in five on pilots (`modem/freedv/ofdm_mode.c`: `tcp`, `ts`, `ns`). The top
  rung, QAM16C2, carries 1213 bytes in 3.7 s: about 2.6 kbit/s
  (`docs/CAROUSEL-SPEC.md`, 1.3). Its code is rate 0.6.
- A VHF/UHF line-of-sight path has microseconds of delay spread and, between
  fixed stations, almost no Doppler. What the modem sees is the radio: the
  audio filters of an FM transceiver (roughly 300-3000 Hz, with group delay
  at the edges), pre-emphasis and de-emphasis, the deviation limiter, the
  discriminator's noise (which rises with audio frequency) and its threshold:
  above it the audio SNR is high, below it the link collapses in clicks.
- Turn-taking constants in `datalink_arq/carousel.c` (`D_OFDM_MS`, the gaps)
  were set from HF sessions and SSB radios. An FM radio adds its own:
  transmit attack, squelch opening, sometimes a repeater's tail.
- Codec2's FSK modem and its VHF framing are vendored and built
  (`modem/freedv/fsk.c`, `freedv_fsk.c`, `fmfsk.c`, `freedv_vhf_framing.c`);
  `modem/modem_freedv.c` knows `FREEDV_MODE_FSK_LDPC` but the ladder does not
  use it.
- What others report on this kind of link (their figures, not measured
  here): VARA FM about 12-13 kbit/s through the microphone and speaker path
  and about 25 kbit/s through a 9600-baud data port; modem73 (public domain,
  read for this proposal) runs OFDM from BPSK to 4096-QAM at code rates 1/4
  to 5/6 in 2400 Hz and lists, as the SNR each needs, 64-QAM 3/4 at 19 dB,
  256-QAM 3/4 at 24 dB, 1024-QAM 3/4 at 29 dB and 4096-QAM 5/6 at 35 dB.
- Regulation, United States (47 CFR part 97; read the current text before
  relying on this, and other countries differ):
  - spread-spectrum emission is authorised only on the bands whose row in
    the table of §97.305(c) lists it: 222 MHz and above, not 6 m or 2 m;
  - §97.311: at most 10 W PEP, never to obscure the meaning of a
    communication, no harmful interference to other emissions and no
    protection from them;
  - §97.307(f): data on 6 m and 2 m up to 19.6 kilobauds, or 20 kHz of
    bandwidth for unspecified codes; on 1.25 m and 70 cm up to 56 kilobauds,
    or 100 kHz; no such limit from 33 cm up. (HF: 2.8 kHz since 2024.)

## What spread spectrum would and would not add

- **Against noise: nothing.** What decodes a frame is the energy per bit
  against the noise density. The same bits at the same power need the same
  Eb/No whether they occupy 2.4 kHz behind a low-rate code or 100 kHz behind
  a spreading sequence. Mercury's floor is already orthogonal signalling
  (32-tone MFSK), the family LoRa's chirps belong to.
- **For speed: the opposite.** Bandwidth spent on spreading carries no data.
  At the SNRs of a line-of-sight link, bandwidth filled with OFDM carriers
  buys rate in proportion.
- **What it does buy:** frequency diversity where the whole narrow channel
  fades at once (mobile and urban paths), tolerance of narrowband
  interferers, several links sharing a frequency, and a low spectral
  density. A wide OFDM signal with a code across its carriers gets the first
  two as well.
- **Hardware:** an FM transceiver cannot emit it. Its occupied bandwidth is
  set by the FM modulator whatever the audio is. Anything wider than a data
  port's few kHz of baseband needs a transmitter driven with I/Q.

So spreading is not the first step. It becomes worth testing only as part of
step C below, for links with interference or mobile fading, against a wide
OFDM signal of the same bandwidth on the same bench.

## Design

### A. FM-audio modes (any FM radio and a sound interface)

New entries beside the HF ones in `ofdm_mode.c`, reached only under the
profile:

| parameter | HF data modes | starting point for FM audio |
|---|---|---|
| cyclic prefix | 6 ms (4 ms) | 1-2 ms: sized to the radios' audio group delay, measured |
| pilot symbols | 1 in 5 | 1 in 8 to 1 in 16, plus proposal 20 between them |
| carriers | 300-2700 Hz region | the flat part of the radio's audio band, measured per data port / mic path |
| constellations | QPSK, 16-QAM | QPSK to 256-QAM |
| code rates | about 1/2 to 0.6 | 1/2, 2/3, 3/4, 5/6 (the codes of proposal 15) |

Plus two things HF does not need:

- **Crest factor under a limiter.** The deviation limiter clips audio peaks;
  the clip-and-filter of proposal 02 sets the crest factor, and the drive
  level is part of the mode's definition (a tune procedure in the UI).
- **Per-carrier noise.** After de-emphasis the noise is not flat across the
  audio band. Proposal 07's per-carrier weights apply as they are; bit
  loading per carrier is a later option.

### B. Link timings

A second table of the carousel's timing constants, selected by the profile
and negotiated at connect (both ends must use the same table): decode
allowance, gaps, key-up lead (TX attack + squelch), tail. Each entry gets
its value from a measurement with real FM radios, as the HF ones did, and
`specs/turns` is checked with the second table. The HF table is not touched.

### C. Wider than a voice channel (research)

Only with a data port (about 6 kHz of baseband) or an I/Q transmitter. The
modem runs at 8 kHz today; the HL2 backend already resamples to 48 kHz. A
20 kHz signal on 2 m or a 100 kHz signal on 70 cm is a different sample
rate, a different radio backend and, in several countries, a different
paragraph of the rules. Scope it after A and B have numbers. This is also
where a spread or hopped variant would be compared, on the bench, against
plain wide OFDM under an interferer and under flat fading.

### Constant-envelope rung

Below the FM threshold OFDM audio is useless, and class-C amplifiers and
direct-FSK data ports want a constant envelope. Bench the vendored 4FSK +
LDPC mode as the profile's floor before designing anything new.

## Bench

`utils/impair` gains an FM path: pre-emphasis, limiter, frequency
modulation, RF noise, discriminator, de-emphasis, the audio band-pass with
its group delay, and optional flat Rayleigh/Rician fading for a mobile
case. Calibrate it against two real radios back to back through an
attenuator (the audio SNR against RF level curve, including the threshold)
before trusting any mode result.

## Steps

1. The FM path in the bench and its calibration.
2. Measure the existing rungs through it: this is the baseline, and it says
   how much the cyclic prefix and the pilots cost there.
3. Mode candidates (A) on the bench; pick a ladder of four or five.
4. Timings (B) with two radios on dummy loads; the model check.
5. Integration test with the profile; mixed-profile peers must refuse each
   other cleanly at connect or fall back to `hf`.
6. Constant-envelope floor.
7. Findings document; then scope C.

## Tests

- `profile = hf`: byte-identical behaviour to trunk.
- Each new mode: loopback, the FM bench at three RF levels, sanitizers.
- A station on `vhf-fm` and one on `hf`: the connect outcome is defined and
  tested.

## Acceptance

- Off: identical to trunk.
- On the calibrated FM bench at an RF level 10 dB above threshold, the top
  FM rung carries at least three times QAM16C2's bytes per second of
  airtime.
- Turnaround (end of one station's burst to start of the other's) measured
  with two real radios, and the profile's constants derived from it.
