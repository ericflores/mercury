# 25 — A VHF/UHF profile (issue #25)

**Kind:** new modes and a second set of link timings, selected by
configuration; the new modes are new rungs, so negotiated. Nothing changes
for an HF station.
**Depends on:** 00-bench (it needs an FM channel model and a wideband one),
15 (the constellations and code rates are the same work), 14 (short control
frames: without them the turnaround, not the modem, sets the speed), 23.
**Switch:** `[link] profile = hf | vhf-fm | vhf-wide`, default `hf`.

## Goal

Two things, for two kinds of station:

1. **Through the FM transceivers people already own**, carry several times
   what the HF ladder can, by not paying for an ionosphere that is not
   there.
2. **Through a radio driven with I/Q, use the bandwidth the band allows.**
   On VHF/UHF that is 20 kHz to 100 kHz and more against HF's 2.8 kHz, and
   on a strong link it is worth an order of magnitude at the same transmit
   power.

And settle, with numbers, what spread spectrum would add.

## Facts

- The ladder was built for HF. Every OFDM data mode spends 6 ms of each
  22 ms symbol on the cyclic prefix (4 of 20 ms for QAM16C2) and one symbol
  in five on pilots (`modem/freedv/ofdm_mode.c`: `tcp`, `ts`, `ns`). The top
  rung, QAM16C2, carries 1213 bytes in 3.7 s: about 2.6 kbit/s
  (`docs/CAROUSEL-SPEC.md`, 1.3). Its code is rate 0.6.
- A VHF/UHF line-of-sight path has microseconds of delay spread and, between
  fixed stations, almost no Doppler.
- **Bandwidth a data signal may occupy, United States** (47 CFR §97.307(f);
  read the current text before relying on this, and other countries and
  their band plans differ): 2.8 kHz on HF; 20 kHz on 6 m and 2 m; 100 kHz on
  1.25 m and 70 cm; no such limit from 33 cm up.
- **What limits the bandwidth in practice is the radio**, not the rule:

  | radio path | what the modem can occupy | notes |
  |---|---|---|
  | FM transceiver, microphone and speaker | about 300-3000 Hz of audio | the RF signal is 11-16 kHz wide whatever the audio carries |
  | FM transceiver, 9600-baud data port | about 6 kHz of baseband | still through the FM modulator and discriminator |
  | all-mode transceiver in SSB | its SSB filter, 2.4-3.6 kHz | today's HF modes run here as they are, if the frequency error is small enough (below) |
  | a transmitter and receiver driven with I/Q (an SDR) | whatever its sample rate and the rules allow | the only path to 20 kHz and more of **linear** channel |

- The OFDM engine takes its sample rate from the mode table (`config->fs`,
  from which `m` and `ncp` follow, `ofdm.c`), but every mode sets 8000, the
  band-pass prototypes (`filtP650S900`, `filtP200S400`) are designed for
  8 kHz, and `modem/modem.c` assumes 8000 throughout (`FREEDV_FS_8000`).
- The one I/Q backend is the Hermes-Lite 2's (`audioio/hl2*`): 48 kHz I/Q on
  transmit, 48 to 384 kHz on receive (`hl2_wire.h`), used today to emulate
  an SSB transceiver (`hl2_dsp`, 200-2950 Hz). The radio itself stops at
  38.4 MHz; VHF/UHF needs a transverter or another SDR.
- **Frequency error.** One part per million is 145 Hz on 2 m and 435 Hz on
  70 cm. The HF modes' carriers are 62.5 Hz apart. Through an FM radio this
  does not matter (the discriminator returns the audio at its own
  frequencies); through SSB or I/Q it decides the carrier spacing and the
  acquisition range.
- Turn-taking constants in `datalink_arq/carousel.c` (`D_OFDM_MS`, the gaps)
  were set from HF sessions and SSB radios, and the control frame lasts
  3.7 s. A 1200-byte frame at 100 kbit/s lasts a tenth of a second.
- Codec2's FSK modem and its VHF framing are vendored and built
  (`modem/freedv/fsk.c`, `freedv_fsk.c`, `fmfsk.c`, `freedv_vhf_framing.c`);
  `modem/modem_freedv.c` knows `FREEDV_MODE_FSK_LDPC` but the ladder does not
  use it.
- What others report through FM radios (their figures, not measured here):
  VARA FM about 12-13 kbit/s through the microphone path and about
  25 kbit/s through a 9600-baud port; modem73 runs OFDM from BPSK to
  4096-QAM at code rates 1/4 to 5/6 in 2400 Hz and lists, as the SNR each
  needs, rate-1/4 QPSK sent twice at −1 dB, QPSK 1/2 at 5 dB, QPSK 3/4 at
  8 dB, 16-QAM 1/2 at 11 dB, 16-QAM 3/4 at 14 dB, 64-QAM 3/4 at 19 dB,
  256-QAM 3/4 at 24 dB.

## What the bandwidth buys (arithmetic, not a measurement)

The same transmit power, the same path, three channel widths. Capacity is
Shannon's; "practical" takes modem73's thresholds above (and 2 dB for
rate-1/4 QPSK sent once) with 3 dB of margin and 25 % of the band lost to
prefix, pilots and guard bands.

| link (SNR it gives in 2.4 kHz) | 2.4 kHz | 20 kHz | 100 kHz |
|---|---|---|---|
| strong, 30 dB: capacity | 24 kbit/s | 138 kbit/s | 464 kbit/s |
| strong, 30 dB: practical | 256-QAM 3/4, about 11 kbit/s | 16-QAM 3/4, about 45 kbit/s | QPSK 3/4, about 110 kbit/s |
| medium, 15 dB: capacity | 12 kbit/s | 45 kbit/s | 82 kbit/s |
| medium, 15 dB: practical | 16-QAM 1/2, about 4 kbit/s | QPSK 1/4, about 8 kbit/s | needs codes below rate 1/4 |
| weak, 5 dB: capacity | 4.9 kbit/s | 9.3 kbit/s | 10.6 kbit/s |

Three readings:

- **On a strong link the width is the whole game**: ten times the data at
  the same power, and with QPSK or 16-QAM instead of 256-QAM, which asks far
  less of the radios' linearity and phase noise.
- **On a weak link it is worth at most a factor of two.** With the power
  fixed, capacity stops growing at `1.44 × C/No` however wide the signal
  (10.9 kbit/s in the last row). That regime is limited by power, not by
  bandwidth.
- The wide profile therefore needs **both ends of a ladder**: dense rungs
  are pointless (QPSK and 16-QAM cover it), but rungs at rate 1/4 and below
  are what let a 100 kHz signal keep working when the path weakens.

## What spread spectrum would and would not add

- **Sensitivity: nothing beyond a low-rate code.** Spreading a signal with
  a sequence is a repetition code: it trades bandwidth for SNR per hertz
  one for one and gains nothing in energy per bit. A rate-1/4 or rate-1/8
  code over the same bandwidth does the same trade **and** adds coding
  gain. So the "weak link" rungs of the wide ladder are low-rate codes on
  the same OFDM signal, optionally with the frame repeated (as modem73's
  "x2" modes do), not a spreading sequence.
- **Speed: the opposite.** Bandwidth spent on a sequence carries no data.
- **What it does buy:** tolerance of narrowband interferers, several links
  sharing a frequency, a low spectral density, and frequency diversity
  where a whole narrow channel fades at once. A wide OFDM signal with a
  code across its carriers gets the first and the last as well.
- **Rules, United States** (read the current text; other countries differ):
  spread-spectrum is its own emission type, authorised only on the bands
  whose row in §97.305(c) lists it (222 MHz and above, not 6 m or 2 m), at
  10 W PEP at most, never to obscure meaning, and with no protection from
  other emissions (§97.311). A coded OFDM signal inside the band's data
  bandwidth is an ordinary data emission with none of those conditions.
  Which of the two a given signal is, is for the licensee to determine.

So: wide, yes, and as soon as there is a radio path for it; spread with a
sequence, only as a later comparison against wide OFDM of the same width
under an interferer and under flat fading.

## Design

### A. FM-audio modes (any FM radio and a sound interface)

New entries beside the HF ones in `ofdm_mode.c`, reached only under
`vhf-fm`:

| parameter | HF data modes | starting point for FM audio |
|---|---|---|
| cyclic prefix | 6 ms (4 ms) | 1-2 ms: sized to the radios' audio group delay, measured |
| pilot symbols | 1 in 5 | 1 in 8 to 1 in 16, plus proposal 20 between them |
| carriers | 300-2700 Hz region | the flat part of the radio's audio band, measured per data port / microphone path |
| constellations | QPSK, 16-QAM | QPSK to 256-QAM |
| code rates | about 1/2 to 0.6 | 1/2, 2/3, 3/4, 5/6 (the codes of proposal 15) |

Plus two things HF does not need:

- **Crest factor under a limiter.** The deviation limiter clips audio peaks;
  the clip-and-filter of proposal 02 sets the crest factor, and the drive
  level is part of the mode's definition (a tune procedure in the UI).
- **Per-carrier noise.** After de-emphasis the noise is not flat across the
  audio band. Proposal 07's per-carrier weights apply as they are; bit
  loading per carrier is a later option.

A 9600-baud data port is the same design over about 6 kHz of baseband at a
16 kHz modem rate.

### B. Link timings and control

A second table of the carousel's timing constants, selected by the profile
and negotiated at connect (both ends must use the same table): decode
allowance, gaps, key-up lead (transmit attack and squelch), tail. Each
entry gets its value from a measurement with real radios, as the HF ones
did, and `specs/turns` is checked with the second table. The HF table is
not touched.

For the wide profile the 3.7 s control frame would cost more than the data
it asks for: proposal 14's short control frames are a prerequisite there,
not an optimisation.

### C. The wide profile (a radio driven with I/Q)

The part that uses the bandwidth. Larger work than A, in three steps that
each stand on their own.

**C1. The modem at a higher rate, on the bench.** A complex-baseband mode
family at 48 kHz (the rate the HL2 path already carries), occupying 20 kHz:

| parameter | starting point | why |
|---|---|---|
| carrier spacing | 500 Hz (useful symbol 2 ms) | 1 ppm of frequency error on 70 cm is 435 Hz; 62.5 Hz carriers cannot be acquired or held there, and a moving station adds tens of hertz of Doppler |
| cyclic prefix | 0.25 ms (12 samples) | radio filters and local echoes; a line-of-sight path needs no more |
| carriers | 36-40 in 20 kHz | leaves guard bands inside the channel |
| pilots | a pilot symbol every 16-32 symbols, and a few pilot carriers in every symbol | the channel is static; the pilot carriers follow oscillator phase and residual frequency error |
| acquisition range | ±2 kHz | ±5 ppm on 70 cm between two stations |
| constellations and rates | QPSK and 16-QAM at 1/4, 1/2, 3/4; 64-QAM 3/4 at the top | see "what the bandwidth buys": no need for denser, real need for lower |
| weak-link rungs | rate 1/4 with the frame sent twice or four times and combined | the efficient form of spreading |

Needs: the engine's filters for the new rate, `modem/modem.c` freed from
its 8000 assumption for these modes only, new LDPC sizes where proposal 15
does not already supply them. Bench: complex noise, a frequency error of
±2 kHz, oscillator phase noise, a power-amplifier model (compression at the
peaks), a sample-clock error, and flat Rician and Rayleigh fading for a
mobile case.

**C2. A radio.** A generic I/Q backend beside `audioio/hl2*` (one library
that covers the common SDRs is the obvious route; evaluate before choosing),
with transmit/receive switching and its latency measured. An HL2 with a
transverter can serve as the first test set, since its backend exists.
OFDM's crest factor through a linear amplifier costs average power: at
10 W PEP expect 1.5-2.5 W average after proposal 02's clipping. Measure the
adjacent-channel power before any on-air use.

**C3. 100 kHz** at a 192 kHz rate for 70 cm and up: the same family with
five times the carriers.

### D. Constant-envelope rungs

Below the FM threshold OFDM audio is useless, and class-C amplifiers and
direct-FSK data ports want a constant envelope. Bench the vendored 4FSK +
LDPC mode as the FM profile's floor. For the wide profile, a
constant-envelope rung (GMSK or 4FSK at tens of kilobauds) puts 6-8 dB more
average power through the same amplifier than OFDM and is the fair
comparison for a weak link: bench the two at equal peak power.

### E. Spread or hopped variants (research)

Only after C1: the same bandwidth, the same bench, an interferer and flat
fading, against wide OFDM.

## Steps

1. The FM path in the bench (pre-emphasis, limiter, frequency modulation,
   RF noise, discriminator, de-emphasis, audio band-pass with its group
   delay), calibrated against two real radios back to back through an
   attenuator, including the threshold.
2. The existing rungs through it: the baseline, and how much the cyclic
   prefix and the pilots cost there.
3. FM-audio mode candidates (A) on the bench; pick a ladder of four or five.
4. Timings (B) with two radios on dummy loads; the model check.
5. Integration test with `vhf-fm`; mixed-profile peers must refuse each
   other cleanly at connect or fall back to `hf`.
6. The wide bench (C1's channel model) and the 20 kHz family on it. This is
   the step that says what the bandwidth is worth in Mercury's own numbers.
7. Proposal 14's short control frames under the wide timings; a simulated
   session at 20 kHz.
8. The I/Q backend and a first pair of radios (C2).
9. 100 kHz (C3); constant-envelope rungs (D); findings document.

## Tests

- `profile = hf`: byte-identical behaviour to trunk.
- Each new mode: loopback, its bench at three levels, sanitizers.
- A frequency error of ±2 kHz and a clock error of ±50 ppm on every wide
  mode.
- Stations on different profiles: the connect outcome is defined and
  tested.

## Acceptance

- Off: identical to trunk.
- FM audio: on the calibrated FM bench at an RF level 10 dB above
  threshold, the top rung carries at least three times QAM16C2's bytes per
  second of airtime.
- Wide, 20 kHz: at a carrier-to-noise density of 64 dB-Hz (a link that
  gives 30 dB in 2.4 kHz) at least 40 kbit/s of payload on the bench, and a
  rung that still works at 49 dB-Hz.
- Turnaround measured with real radios for each profile, and the profile's
  constants derived from it.
