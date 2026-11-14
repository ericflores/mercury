# 31 — A deep floor below the MFSK floor (issue #31)

**Kind:** new mode family, wire change (negotiated). **Depends on:** the
capability scheme; 10 (the Doppler measurement that chooses the alphabet);
04 (copies combined across rounds); 32 (the search over a round and its
copies); 06 shares two of its pieces.
**Switch:** `MERCURY_DEEP_FLOOR=1` / `[arq] deep_floor`, default off; used
only when both ends announced it.

## Goal

A rung under the MFSK floor for links that today die there: long tones from
a large alphabet, a low-rate code, copies combined, found by a search that
knows when the copies come.

## Facts

- Research note with every table: `BELOW-NOISE-RESEARCH.md`. Prototype:
  `utils/deep_floor_sim` (baseband simulation at 2 kHz, the project's
  Watterson model, the modem's LDPC codes and decoder).
- Measured, half the frames delivered, rate-1/3 code `H_256_768_22`:
  128 tones of 64 ms (31.8 bit/s) −15.0 dB white noise, −14.4 CCIR Poor,
  −13.7 NVIS, −13.0 flutter; 256 tones of 128 ms (18.0 bit/s) −17.6, −16.6,
  −16.6, fails at 10 Hz; 512 tones of 256 ms (10.0 bit/s) −20.0, −19.0,
  −19.0. Copies of a 256-tone frame on CCIR Poor: 2 → −19.2, 4 → −21.0,
  8 → −23.0 dB.
- At the same rate the prototype is within about 1 dB of the current floor
  (`docs/MODES.md`: MFSK500 at about 31 bit/s, 90 % at −14.1 dB on white
  noise; the prototype's 128 tones at 31.8 bit/s, 90 % at −14.6 dB). The
  depth is from the rate and the copies.
- The MFSK floor today: `modem/modem_mfsk.{c,h}`, modes 100 (32 tones at
  31.25 Hz), 102 and 103 (500 Hz geometries); the code family is the MFSK
  port's N=1600 codes (`modem/mfsk/mfsk_ldpc.h`); `arq_floor_mode()` picks
  the floor per bandwidth (`datalink_arq/arq.c`).
- Decoders are pooled (`pool_modes[]`, `modem/modem.c:723`), timings are a
  table (`arq_mode_table`, `datalink_arq/arq_protocol.c:61`), the rung is a
  3-bit field with one free value (handbook, "Wire format constraints").
- The Doppler spread is logged per packet by proposal 10 (`dopp=` in the
  `chanstats:` line); it reads on the OFDM rungs, not on MFSK.
- Measured pieces: the bounded search metric (`g_sync_energy` in the
  prototype; one copy found 40 of 40 at −16 dB on CCIR Poor instead of 35);
  per-symbol noise normalisation (`g_norm_sym`; strong impulses 0 → 30 of
  30 at −10 dB, 0.2 dB of cost without them); known symbols spread over the
  frame (a small gain in finding a single copy).
- Measured: under an RTTY-like signal inside the band 20 dB over the floor,
  each bin then each symbol divided by its own median keeps 25 of 30 frames
  at −16 dB (0 of 30 without; research note 4.7). The receiver needs both.
- Measured and rejected: chirps (0.5 to 1.5 dB worse than tones in slow
  fading even with an ideal RAKE); a long rate-1/2 code (2 dB worse under
  fading).

## Design

### Phase 0: the real pipeline (bench, no protocol)

1. A transmitter and receiver at 8 kHz in `utils/` (start from
   `deep_floor_sim`): the tones placed in the passband (500-2500 Hz for
   256 tones of 7.8 Hz), continuous phase, the transmit filter, the SSB
   passband, a frequency offset and a slow drift.
2. Fine estimation of time and frequency after the search: the tone
   energies of the known symbols at fractional offsets (zero-padded FFT or
   interpolation of the peak). Measure the loss against the genie.
3. The table of section 4.1 of the research note again, through this
   pipeline, with `chanutil` (the same channel as `rx_bench`), against the
   MFSK floor's own numbers measured on the same channel
   (`utils/mfsk_burst_file` and `utils/mfsk_channel_sweep.sh`).
   **Stop here if** at 18 bit/s the real pipeline does not reach at least
   −15 dB on CCIR Poor, or if, at the same rate, it is not at least 1 dB
   better than the MFSK floor measured on the same channel; then the floor
   is better served by giving the MFSK floor copies and the search of 32.
4. A lower-rate code (rate 1/4 to 1/6; the protograph families of the
   CCSDS deep-space standards, or repeat-accumulate codes) and, as a second
   step, a code over GF(M) matched to the alphabet with iterative demapping.

### Phase 1: the mode

- Three geometries, chosen by the receiver from the Doppler it measured
  (on the control frames and the OFDM rungs; on the floor itself from the
  spread of the known symbols' tones): 512 tones of 256 ms below 0.5 Hz,
  256 of 128 ms below 3 Hz, 128 of 64 ms above.
- Frame: known symbols in three groups (start, middle, end), the codeword
  interleaved over the frame, a CRC16 seeded with the session as every
  frame is.
- Receiver: the bounded search metric; per-bin then per-symbol
  normalisation; the
  fading-channel symbol metric; log-sum-exp bit LLRs.
- In the pool and the timing table like MFSK; a capability bit; the ladder
  goes below MFSK only when both ends have it.

### Phase 2: copies

With 04 and 32: the receiver asks for the same frames again, the modem adds
the symbol log-likelihoods of every copy before the bit LLRs, and the
search adds up the known symbols over the copies at the expected spacing.

## Tests

- Unit: tone placement and continuity; encode and decode noiseless; the
  bounded metric bounded; per-symbol normalisation neutral on white noise
  within 0.3 dB; with the switch off nothing changes.
- Bench: phase 0's tables; false accepts on noise and on other sessions'
  frames.
- Integration: sessions at −15 to −20 dB on CCIR Poor and NVIS with the
  capability on both ends; a session that would die at the MFSK floor
  completes.

## Acceptance

- Phase 1: a session completes on CCIR Poor at an SNR where every frame of
  the MFSK floor fails, measured on the same channel, with no false accept
  in the controls.
- Phase 2: each doubling of the copies lowers the level at which half the
  frames are delivered by at least 2 dB, down to 8 copies.

## Not in scope

Changing the MFSK floor itself (proposal 06 takes the two pieces that apply
to it); chirps.
