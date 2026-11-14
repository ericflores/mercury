# Below the noise: what decides it, ideas from other fields, and a deep floor measured

Research note, 2026-11-14. The fork's owner asked: which kind of modulation
is most different from the noise? (CW is one.) Explore ideas from other
fields, combined in ways that solve the problem, to tell signals apart below
the noise threshold; "it cannot be done" is not an answer.

Everything marked *measured* was run in this repository on 2026-11-14 with
`utils/deep_floor_sim` (new, in this change), the project's Watterson channel
model and the modem's own LDPC codes and decoder. Everything else is
arithmetic or literature and says so. Nothing here is in the modem.

## Short answer

- **"Below the noise" is not a wall.** SNR in 3 kHz is a reference width.
  What decides is the energy the receiver can gather per bit against the
  noise density: no receiver works below Eb/N0 = −1.6 dB (Shannon), and any
  receiver can work at any SNR in 3 kHz if it is given enough time and knows
  what shape to look for. CW is copied under the noise because the ear is a
  filter some 50 Hz wide (18 dB narrower than 3 kHz) and the operator
  integrates over time.
- **Mercury's floor already works like that.** The MFSK floor delivers at
  −11.2 dB in 3 kHz at about 60 bit/s: Eb/N0 +5.8 dB, the same efficiency as
  FT8 (+5.1 dB by its published threshold). The way further down is the same
  for everybody: **send fewer bits per second, gather the energy of every
  repeat, and keep finding the signal while doing it.** The second and the
  third are where Mercury has something nobody else has: its receiver asked
  for what is being sent, and knows when it comes.
- **Measured, a deep floor made of long tones from a large alphabet** (one of
  256 tones of 128 ms, the modem's rate-1/3 LDPC code, 18 bit/s): half the
  frames at **−17.6 dB** on white noise and **−16.6 dB** on CCIR Poor and on
  the 7 ms NVIS channel, every frame at −15 dB. With 512 tones of 256 ms
  (10 bit/s): **−20.0 dB** white noise, **−19.0 dB** CCIR Poor and NVIS.
- **Measured, repeats combined:** each doubling of the copies moves the
  threshold down about 2 to 2.6 dB (a little less than the 3 dB of the
  energy, because the copies are combined after noncoherent detection):
  −16.6, −19.2, −21.0 and −23.0 dB on CCIR Poor for 1, 2, 4 and 8 copies. And **finding the signal keeps up when
  the search adds up the copies it expects**: with 4 copies the frame is
  found 40 of 40 times at −24 dB, where a single copy is found 9 times.
- **At the same bit rate the prototype is no more than about 1 dB better
  than the current floor** (and the simulation is optimistic: no filters,
  perfect fine synchronisation). The depth comes from the lower rate and the
  copies, as theory says it must. This note says so plainly because it is
  the difference between a new modulation and a new operating point.
- **Ideas that lost or do not apply:** chirps (LoRa's modulation) lose 0.5 to
  1.5 dB to tones in slow fading even with an ideal RAKE, and tie with
  shorter tones in fast fading; stochastic resonance, compressed sensing and
  a coherent pilot gain nothing over this receiver; time reversal needs a
  channel that holds still for the turnaround, which HF does not.
- **The floor can share the band.** With each bin and then each symbol
  divided by its own noise, the 256-tone floor at −16 dB still delivers 25
  of 30 frames with an RTTY-like signal 20 dB over it inside its band
  (0 of 30 without).
- **Two things from other fields that did win:** a search metric that does
  not let one loud window decide (the radar M-of-N rule and LIGO's χ² veto
  in soft form: single-copy synchronisation from 35 of 40 to 40 of 40 at
  −16 dB), and dividing each symbol by its own noise (from hearing: strong
  impulses, 0 of 30 frames to 30 of 30).

## 1. What "below the noise" means

The SNR in 3 kHz compares the signal's power with the noise power in a 3 kHz
band. A receiver does not have to listen to 3 kHz. With a symbol of length
T, the energy per symbol over the noise density is

    Es/N0 = SNR3k + 10 log10(3000 × T)

which for T = 128 ms is SNR3k + 25.8 dB. A tone 17 dB under the noise in
3 kHz is 9 dB over it in its own 7.8 Hz bin. That is the whole trick of CW,
of WSPR, of FT8, of GPS (whose signals arrive 20 to 30 dB under the thermal
noise of its bandwidth), and of deep-space links. The price is the bit rate.

The limit is on energy per bit. For a rate R in 3 kHz, the lowest SNR at
which any modulation and code can work (Shannon):

| bit/s | Shannon limit, SNR3k | Eb/N0 = SNR3k + |
|---|---|---|
| 60 | −18.6 dB | 17.0 dB |
| 31 | −21.4 dB | 19.9 dB |
| 18 | −23.8 dB | 22.2 dB |
| 10 | −26.4 dB | 24.8 dB |
| 4.5 | −29.8 dB | 28.2 dB |
| 2.25 | −32.8 dB | 31.2 dB |

Where the existing weak-signal modes sit (published thresholds, converted to
the 3 kHz reference and to Eb/N0 with their payload rates):

| mode | bit/s | threshold, SNR3k | Eb/N0 | from Shannon |
|---|---|---|---|---|
| Mercury MFSK floor (`docs/MODES.md`, 50 % on white noise) | ~60 | −11.2 dB | +5.8 dB | 7.4 dB |
| FT8 (77 bits in a 12.6 s transmission, −21 dB in 2.5 kHz) | 6.1 | −21.8 dB | +5.1 dB | 6.7 dB |
| WSPR (50 bits in 110.6 s, −28 dB in 2.5 kHz) | 0.45 | −28.8 dB | +9.4 dB | 11 dB |

So **every one of them is 7 to 11 dB from the limit, and the deep ones are
deep because they are slow**, not because they are more efficient. Two
roads lead further down: closer to the limit at the same rate (a few dB at
most, section 5), or slower (without limit, as long as the receiver keeps
finding the signal).

## 2. What is most different from noise

Noise is white and Gaussian: its energy is spread evenly over every shape a
receiver could look for. For a given energy, the signal most unlike it puts
all of that energy into one shape the receiver knows, out of as many
equally likely shapes as possible, and holds it as long as the channel
allows. CW is the simplest case: one tone, on or off. The general case is an
alphabet of M orthogonal shapes, one sent per symbol: one of M tones
(M-FSK), one of M time slots (pulse position, deep-space optical links), one
of M cyclic shifts of a chirp (LoRa). As M grows, even a receiver that does
not track the carrier's phase approaches the Shannon limit (a classical
result for orthogonal signalling; Viterbi, Golomb). Mercury's floor is
32-ary and 16-ary FSK.

On HF two things bound M:

- **The symbol cannot outlast the channel.** A tone must stay in its bin for
  the whole symbol: the bin (1/T) must be wider than the Doppler spread.
  At 1 Hz of spread, T up to about 250 ms; at 10 Hz (flutter), about 64 ms.
- **The alphabet cannot be wider than the passband**, about 2 kHz.

So M ≤ 2000 × T: 128 tones at 64 ms, 256 at 128 ms, 512 at 256 ms. Which one
is right depends on the Doppler spread the receiver measures (proposal 10
already logs it).

## 3. Ideas from other fields

| field | idea | what it would do here | status |
|---|---|---|---|
| Morse by ear | a narrow filter and long integration | one of M tones held for T | the basis of everything measured in section 4 |
| IoT (LoRa), HF chirp sounders, radar pulse compression, bat echolocation | chirp spread spectrum: one of M cyclic shifts of a chirp, dechirp and FFT; multipath delays become bin offsets, so a RAKE turns them into diversity | a floor waveform that needs no adaptation to Doppler and spreads a narrow interferer | **measured, lost** in slow fading (section 4.2); ties with shorter tones in fast fading |
| Pulsar astronomy | epoch folding: add up many periods of a weak periodic signal at the known period | add up the copies of a frame the receiver knows are coming | **measured** (4.4): 2 to 2.6 dB per doubling |
| GPS | assisted acquisition: search only where the signal can be, and integrate long | the receiver knows when the peer answers and how a round is laid out | **measured** (4.4): the search integrated over the copies holds 6 dB lower than one copy |
| Radar | track-before-detect: accumulate evidence along the expected track before deciding there is a target | integrate the preambles of a whole round of frames before deciding a burst is there | proposal 32; the principle measured in 4.4 |
| Radar | M-of-N detection, robust to clutter spikes | a search metric that one loud window cannot dominate | **measured, won** (4.5) |
| Gravitational-wave astronomy (LIGO) | χ² veto: a real signal shows up across all frequency bands of the template; a glitch in one | the same, across the known symbols of the preamble | **measured, won** (4.5, the bounded metric is this in soft form) |
| Hearing (comodulation masking release) | noise that rises and falls at all frequencies at once is measured where the signal is not, and divided out | divide each symbol's bins by that symbol's own median | **measured, won** against strong impulses (4.6) |
| Radio astronomy | record everything, search later | keep the last minutes of audio; when the protocol says a frame must have come, search the stored audio for it with everything known about it | proposal 32 |
| Deep space (JPL) | low-rate concatenated codes; multiple-symbol noncoherent detection; arraying | lower-rate codes for the floor; ~1 to 2 dB from using the phase continuity of the tones; a second receiver (proposal 11) | not measured; section 5 |
| Ham weak-signal (WSJT-X) | codes over the alphabet (Q65's q-ary codes) and a priori decoding with known message parts | a code over GF(256) matched to 256 tones instead of bits; known fields of control frames (proposal 5) | not measured; section 5 |
| Deep space | residual carrier: a pilot tone tracked by a narrow loop, coherent detection | coherent detection of the data | arithmetic: no gain (section 6) |
| Biology, physics | stochastic resonance: noise helps a threshold detector find a weak signal | — | does not apply (section 6) |
| Signal processing | compressed sensing, sparse recovery | — | does not apply (section 6) |
| Underwater acoustics, medical ultrasound | time reversal: send back the received channel reversed so it focuses at the other end | — | does not apply on HF (section 6) |
| Power-line communication | robust metrics for impulsive noise | per-symbol normalisation (above) and the blanker (proposal 08) | measured (4.6) |
| DSL bit loading, proposal 07 | treat each frequency by its own noise | each bin divided by its own median over the frame, against a foreign signal in the band | **measured, won** with the per-symbol step (4.7) |

## 4. Measured: `utils/deep_floor_sim`

Frames of M-ary symbols carrying one LDPC codeword of the modem
(`H_256_768_22`, rate 1/3, 256 bits of which 240 payload after a CRC16,
unless said), 8 known symbols for synchronisation, the codeword's bits
interleaved over the frame. Baseband at 2 kHz (the occupied band), the
Watterson model of `common/watterson.c` with the presets of
`watterson_presets()`, white noise referred to 3 kHz. Noncoherent
fading-channel symbol metrics, bit LLRs by log-sum-exp over the alphabet,
the modem's sum-product decoder. Delivered = every payload bit right. One
seed per point, 30 or 40 frames a point; every waveform of a row sees the
same channel and noise. Synchronisation is perfect for the delivery columns
(the receiver knows where the frame starts and that there is no offset);
section 4.4 measures whether a search would have found it.

### 4.1 Long tones from a large alphabet

SNR3k at which half the frames are delivered (M-FSK, rate 1/3):

| alphabet | symbol | bit/s | white noise | CCIR Moderate (1 ms, 0.5 Hz) | CCIR Poor (2 ms, 1 Hz) | NVIS (7 ms, 1 Hz) | flutter (0.5 ms, 10 Hz) |
|---|---|---|---|---|---|---|---|
| 64 tones | 32 ms | 55.2 | −12.6 | | | | about −11 |
| 128 tones | 64 ms | 31.8 | −15.0 | about −14.0 (shallow) | −14.4 | −13.7 | −13.0 |
| 256 tones | 128 ms | 18.0 | −17.6 | −16.6 | −16.6 | −16.6 | fails (11 of 30 at best) |
| 512 tones | 256 ms | 10.0 | −20.0 | −19.1 | −19.0 | −19.0 | |

Every frame is delivered about 1.5 dB above those levels on CCIR Poor and
NVIS (256 tones: 40 of 40 at −15 dB on both). On CCIR Good (0.1 Hz) the
curves are shallow: a frame sits inside one fade, and it is the fade, not
the noise, that decides (256 tones: 24 of 40 at −16 dB, 37 of 40 at −12).
The 7 ms NVIS channel, which defeats the OFDM rungs, costs a tone nothing:
a tone is a flat channel whatever the delays.

The alphabet has to follow the Doppler: 256 tones fail at 10 Hz (bins of
7.8 Hz), 128 tones work there (26 to 29 of 30 at −12 dB on flutter, the
low-latitude disturbed and the high-latitude moderate presets).

In energy per bit, the 256-tone row on white noise is at Eb/N0 +4.6 dB
(6.2 dB from Shannon at its rate), the 512-tone row at +4.8 dB.

### 4.2 Chirps against tones (same alphabet, symbol, band, code, channel)

256-ary, half the frames delivered:

| channel | tones | chirps | chirps + RAKE from the preamble | chirps + RAKE told the paths |
|---|---|---|---|---|
| white noise | −17.6 | −17.6 | | |
| CCIR Moderate | −16.6 | never more than 5 of 40 | −15.8 | −16.3 |
| CCIR Poor | −16.6 | 1 of 40 at best | −15.7 | −16.2 |
| NVIS (7 ms) | −16.6 | 0 | −15.0 | −15.5 |
| flutter (10 Hz) | fails | | 29 of 30 at −12 dB | |

- Without the RAKE a chirp is useless under multipath: a path τ late
  dechirps τ × 2000 bins lower, where it looks like another symbol.
- With the RAKE it works, and still loses 0.5 to 1.5 dB to plain tones in
  slow fading: combining the energy of several bins adds their noise too,
  while a tone gets all its paths in one bin and lets the code ride the
  fades. The ideal RAKE (told the path delays) closes only half the gap.
- Under 10 Hz of Doppler, 256-ary chirps with the RAKE work where 256 long
  tones fail; but 128 shorter tones do as well (above). Choosing the
  alphabet from the measured Doppler beats a waveform that tolerates all of
  it. **Chirps: not proposed.**

### 4.3 The code

256 tones on white noise: the long `H_1024_2048_4f` (rate 1/2, a 34 s frame,
29.8 bit/s) needs only Eb/N0 +3.3 dB (−16.7 dB at 50 %, a cliff 0.5 dB
wide), 1.3 dB better than the rate-1/3 code. Under fading it is the other
way round: on CCIR Poor, NVIS and CCIR Moderate the long rate-1/2 frames
need −14.2 to −14.8 dB, 2 dB more than the rate-1/3 ones. Lower rate rides
the fades. The deep floor wants low-rate codes.

### 4.4 Copies, and finding them

Copies of the same frame, one after the other through the same continuing
channel, combined by adding the symbol log-likelihoods (what proposal 04
would let the carousel do). 256 tones, rate 1/3.

| copies | bit/s | CCIR Poor, 50 % | NVIS, 50 % |
|---|---|---|---|
| 1 | 18.0 | −16.6 | −16.6 |
| 2 | 9.0 | −19.2 | −19.0 |
| 4 | 4.5 | −21.0 | −20.8 |
| 8 | 2.25 | −23.0 | −22.6 |

2.6, 1.8 and 2.0 dB per doubling on CCIR Poor: the copies are combined
after noncoherent detection, where each copy's own SNR is low, which costs
part of the 3 dB the extra energy would be worth. Combining before
detection would need the channel's phase to hold from copy to copy, which
it does not over 13 s.

The search: the 8 known symbols' metric at the true time against the
largest at every other time in ±2 s (steps of 16 ms, every bin), for one
copy alone, and added over the copies at the spacing the receiver expects.
Frames found of 40:

| SNR3k | CCIR Poor, one copy | 2 copies | 4 copies | 8 copies |
|---|---|---|---|---|
| −18 | 36 | 40 | 40 | 40 |
| −20 | 31 | 38 | 40 | 40 |
| −22 | 22 | 34 | 40 | 40 |
| −24 | 10 | 20 | 40 | 40 |
| −26 | 6 | 14 | 37 | 40 |

With 8 copies the search finds the frame 40 of 40 times down to −28 dB on
both channels. The search integrated over the copies stays ahead of the
decoder at every number of copies: **the frame can be found where it can be
decoded, as long as the receiver knows when the copies come.** Without that
knowledge each copy would have to be found alone, and that is what would
set the floor: with 4 copies the decoder delivers 21 of 40 at −21 dB, where
each copy alone is found 26 times of 40, and with 8 copies 21 of 40 at
−23 dB, where each copy alone is found 16 times. That is the situation the
narrow OFDM rungs and the MFSK floor are in today (`docs/MFSK-PORT.md`:
"delivered == acquired").

Known symbols spread over the frame (3 at the start, 2 in the middle, 3 at
the end, as FT8 spreads its Costas arrays) instead of a preamble: one copy
found 38 of 40 at −18 dB instead of 36, and 33 instead of 31 at −20 dB on
CCIR Poor. A small gain; worth having, as proposal 31 says.

### 4.5 A search metric that one loud window cannot dominate

The first version of the search added the energies of the known symbols at
each candidate time and offset. On CCIR Poor it missed 4 to 5 frames of 40
even at −12 dB, where every frame decodes. Printing the misses showed why:
the winning wrong candidates sat inside the data, where one data tone that
happens to fall on the searched bin, in a moment when the channel is up,
outweighs the eight known symbols in a moment when it is down.

The fix comes from radar (M-of-N detection: count how many looks agree,
not how loud one is) and from gravitational-wave astronomy (the χ² veto: a
real signal appears in every band of the template, a glitch in one). Each
window now adds the posterior log-odds that its tone is the expected one
rather than any of the 256, which can never exceed ln 256. One copy, CCIR
Poor, frames found of 40:

| SNR3k | energy sum | bounded log-odds |
|---|---|---|
| −12 | 35 | **40** |
| −14 | 36 | **40** |
| −16 | 35 | **40** |
| −18 | 32 | **36** |
| −20 | 29 | **31** |

The same applies to any acquisition that looks for known symbols among data
symbols of the same alphabet, the MFSK floor's included (proposal 06).

### 4.6 Each symbol divided by its own noise

Strong impulses (2 ms bursts of noise, 5 a second) raise every bin of the
symbols they hit. Dividing each symbol's bins by that symbol's own median
bin, instead of one noise level for the frame, makes such a symbol look
like all the others (the idea of comodulation masking release in hearing:
noise that rises at every frequency at once is measured where the signal is
not). 256 tones, white noise plus impulses, frames of 30, frame-wide / per
symbol:

| impulses | −14 dB | −12 dB | −10 dB |
|---|---|---|---|
| 25 dB over the signal | 30 / 30 | 30 / 30 | 30 / 30 |
| 35 dB over the signal | 0 / 19 | 15 / 24 | 22 / 30 |
| 45 dB over the signal | 0 / 9 | 0 / 19 | 0 / 30 |
| 30 a second, 35 dB | 0 / 0 | 0 / 0 | 0 / 0 |

Without impulses it costs about 0.2 dB (−17.6 → −17.4 dB at 50 %). In the
modem the blanker of proposal 08 would act first; this is the metric's own
safety net, and a candidate for the MFSK floor's receiver today.

### 4.7 Someone else's narrow signal inside the floor's band

A shared band has other signals in it. An RTTY-like signal (two-tone FSK,
45 baud, 170 Hz shift) 300 Hz from the centre of the 256-tone band, its
level relative to the floor's whole power, white noise. Its tones are
louder than the floor's in every symbol, so the receiver takes them for
the transmitted symbol. Each bin divided by its own median over the frame
(the noise-aware weights of proposal 07, applied to the floor; only bins
more than 3 dB over the frame's noise are touched, because scaling every
bin by the scatter of a 96-sample median cost about 1 dB at the cliff),
then each symbol by its own median (4.6). Frames of 30, at −16 / −14 /
−12 dB:

| foreign signal | as received | per bin | per symbol | per bin, then per symbol |
|---|---|---|---|---|
| level with the floor | 30 / 30 / 30 | 30 / 30 / 30 | | 30 / 30 / 30 |
| 10 dB over | 0 / 0 / 0 | 0 / 2 / 15 | 0 / 0 / 0 | **29 / 30 / 30** |
| 20 dB over | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | **25 / 30 / 30** |
| 30 dB over | 0 / 0 / 0 | 0 / 0 / 0 | | **0 / 24 / 30** |

Neither normalisation alone is enough; the two together keep the floor
working under a foreign signal 20 dB over it, which costs the code only a
few of its 256 tones. Without a foreign signal they change nothing
(7, 25, 39, 40 of 40 at −18, −17.5, −17, −16.5 dB with and without).
Chirps with the RAKE did worse here than without any foreign signal and
were not rescued by the same normalisation (at 10 dB over: 10 / 14 / 1 of
30 as received, 4 / 11 / 0 normalised; nothing at 20 dB): dechirping
spreads the foreign signal over every bin, and in this receiver it also
upsets the path profile measured on the preamble. Another point against
chirps for the floor.

## 5. Where this leaves the floor, honestly

- **At the same bit rate, about the same as today.** 128 tones at 31.8
  bit/s reach 90 % at −14.6 dB on white noise; the MFSK500 floor at about
  31 bit/s reaches 90 % at −14.1 dB (`docs/MODES.md`). 0.5 dB, inside what
  the simulation's optimism (no filters, perfect fine synchronisation) can
  account for. Mercury's floor is already a good member of this family.
- **Deeper by being slower and by gathering copies.** 18 bit/s at −16.6 dB
  on CCIR Poor and NVIS, 10 bit/s at −19 dB, 4.5 bit/s (4 copies) at −21 dB,
  2.25 bit/s (8 copies) at −23 dB, each copy found by a search that knows
  when it comes. For comparison, the
  MFSK floor's published fading figure is 90 % at −10.4 dB on moderate
  fading at about 31 bit/s.
- **What is left before the limit**, by the measurements and the
  literature: the rate-1/3 code over bits loses about 1.5 dB against a code
  matched to the 256-ary alphabet (Q65's way) and decoded with the
  demapper iteratively; noncoherent detection of independent symbols loses
  about 1 to 2 dB against detection that uses the tones' phase continuity
  over a few symbols (multiple-symbol noncoherent detection, from deep-space
  links), where the Doppler is slow; the 8 known symbols cost 0.35 dB. That
  is 3 to 4 dB in all, of the 6 to 7 dB to Shannon; the rest is the price of
  not knowing the channel's phase, and no receiver gets it back.

## 6. Ideas that do not apply, and why

- **Stochastic resonance.** Adding noise helps a detector that only sees
  whether its input crosses a threshold (a neuron, a 1-bit converter). A
  receiver that sees the whole signal, as this one does, loses with every
  bit of added noise.
- **Compressed sensing.** It recovers sparse signals from few measurements;
  against white Gaussian noise it cannot beat the matched filter, which is
  what the FFT bins already are.
- **A residual carrier (deep space).** A pilot taking 20 % of the power,
  tracked by a 2 Hz loop, gives a phase reference good to about 0.4 rad at
  −20 dB, so coherent detection of the rest: arithmetic gives about 17
  bit/s at −20 dB, against 14 bit/s for 256 noncoherent tones. Under 1 Hz of
  fading the loop loses lock in the fades, which the tones do not mind. Not
  worth a waveform of its own.
- **Time reversal.** The receiver would send back the channel it measured,
  reversed, so the peer's signal focuses at it. HF phases change in about
  0.3 s at 1 Hz of Doppler, and the turnaround takes seconds. What does
  survive the turnaround is the frequency offset and the delay profile; the
  frequency can be pre-corrected so the peer searches a narrower window
  (proposal 32).

## 7. Proposals

- **Proposal 31 (issue #31): a deep floor.** Long tones from an alphabet
  chosen from the measured Doppler (128, 256 or 512), a low-rate code, known
  symbols spread over the frame, the bounded search metric, per-symbol noise
  normalisation, copies combined. A negotiated mode below the MFSK floor:
  18 bit/s at −16.6 dB on CCIR Poor and NVIS in this simulation, 10 bit/s at
  −19 dB, deeper with copies.
- **Proposal 32 (issue #32): acquisition that uses what the protocol knows.**
  The receiver asked for the round: it knows the mode, the number of frames,
  their spacing and roughly when the first one comes. It keeps the audio of
  that window, and searches it with the known preamble of every frame of
  the round added up (track-before-detect), then decodes each frame at the
  timing found, and combines repeats. For the deep floor this is section
  4.4; for the narrow OFDM rungs, which are acquisition-limited at their
  cliffs, it is the same idea with their preambles.
- **Proposal 06 (MFSK floor metrics), two additions:** the bounded search
  metric for its acquisition, and per-symbol noise normalisation in its LLRs.

## Caveats

- Baseband simulation of the waveform and the code: no transmitter or
  receiver filters, no clipping, no frequency offset, perfect fine timing in
  the delivery columns. Expect the real thing to need 1 to 2 dB more.
- One seed per point, 30 or 40 frames: differences of a few frames are
  noise.
- The comparison with the MFSK floor and with FT8 and WSPR uses their
  published thresholds, measured on other pipelines.
- The copies run through one continuing channel; how much they gain under
  slow fading depends on how far apart they are sent (the CCIR Good curves
  show why).
- The search measurement compares the true alignment with every other; it
  does not implement the fine estimation of time and frequency that would
  follow.

## Reproducing

```sh
make -C modem/freedv libfreedvdata.a && make -C utils deep_floor_sim
cd utils
# waveforms: tones, chirps, chirps with the RAKE measured, told the paths
./deep_floor_sim fsk,css,cssr,cssg 256 H_256_768_22 ccir-poor -20 -8 1 40
# alphabet against Doppler
./deep_floor_sim fsk 512 H_256_768_22 itur-mdv -22 -14 0.5 30
./deep_floor_sim fsk,cssr 128 H_256_768_22 ccir-flutter -18 0 2 30
# copies, and the search per copy and integrated (last two arguments)
./deep_floor_sim fsk 256 H_256_768_22 ccir-poor -28 -14 1 40 4 1
# a foreign RTTY-like signal 20 dB over the floor, with both normalisations
DFS_FSK=300,20 DFS_NORM=both ./deep_floor_sim fsk 256 H_256_768_22 awgn -16 -12 2 30
# the old search metric, known symbols spread, impulses with per-symbol normalisation
DFS_SYNCSTAT=energy ./deep_floor_sim fsk 256 H_256_768_22 ccir-poor -20 -12 2 40 1 1
DFS_SYNC=spread ./deep_floor_sim fsk 256 H_256_768_22 ccir-poor -22 -12 1 40 1 1
DFS_IMP=5,45 DFS_NORM=sym ./deep_floor_sim fsk 256 H_256_768_22 awgn -16 -10 2 30
```
