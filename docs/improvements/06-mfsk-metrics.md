# 06 — MFSK floor soft metrics (issue #6)

**Kind:** receiver only. No wire change. **Depends on:** 00-bench
(impairments).
**Switches (independent, default off):** `MERCURY_MFSK_WHITEN`,
`MERCURY_MFSK_EXACT`, `MERCURY_MFSK_ID`.

## Goal

Make the floor robust to narrowband interference and coloured noise, then
recover the sensitivity that the simplest soft metric leaves unused.

## Facts

- `modem/mfsk/mfsk.c`, `mfsk_demod()` (about 392-475). Input `fft_in`:
  `nSymbols × Nc` complex bins. Per symbol: `noise_var` = mean energy of the
  stream's `M` bins except the strongest; per bit, `llr = (max_E0 − max_E1) /
  (2·noise_var)`, clamped to ±5. Tones hop: `actual = (tone + s·tone_hop_step)
  % M`.
- The comment at 396-401 records an 8 dB loss when the noise estimate did not
  match the noise on the tone bins (behind a 500 Hz filter).
- Receive flow in `modem/modem_mfsk.c`: `mfsk_demod(&h->m, h->rb, h->nb,
  h->llr)` (line 947), de-interleave into `h->llr_di` (951), then
  `mfsk_ldpc_decode_ws(h->ldpc, h->code, h->llr_di, out_bits, 50)` (962,
  1129). HARQ accumulation exists (`h->llr_acc`).
- LDPC: binary, `modem/mfsk/mfsk_ldpc*.c`, rate 8/16, N = 1600, 50
  iterations.
- Instruments: `utils/mfsk_channel_sweep.sh`, `utils/mfsk_rx_probe.c`,
  `utils/mfsk_burst_file.c`, `tests/modem/test_mfsk*.c`.
- Published references: MODES.md gives 50 % FER at −11.2 dB AWGN (1 kHz
  MFSK), 90 % delivery at −14.1 dB for MFSK500.

## Design

### 1. Per-tone noise floor (`MERCURY_MFSK_WHITEN`)

Before the per-symbol loop, for each physical bin `b` in the stream:

```
floor[b] = median over symbols s of |fft_in[s][b]|²
```

The transmitted tone visits a given bin in about 1 symbol of 32, so the
median is that bin's noise-plus-interference power. Then use normalised
energies `E'[s][b] = E[s][b] / floor[b]` in place of raw energies, and
`noise_var = 1` (or the per-symbol mean of the normalised non-peak bins, to
keep tracking time variation). A steady carrier in bin `b` has `floor[b]`
equal to its own power and no longer wins the max in every symbol.

Guard: if a bin's floor is more than 20 dB above the median floor, treat the
bin as erased (energy 0) rather than dividing by a huge number.

Frames have `nb / (nBits·nStreams)` symbols; check there are at least ~60
symbols so the median is stable (true for the 98-byte frame).

### 2. Exact non-coherent metric (`MERCURY_MFSK_EXACT`)

For orthogonal non-coherent detection with known noise variance σ² and
signal amplitude A per symbol, the likelihood of tone m is proportional to
`I0(A·sqrt(E_m)/σ²)`. Bit LLR:

```
llr_k = log Σ_{m: bit k = 0} I0(a·sqrt(E_m)) − log Σ_{m: bit k = 1} I0(a·sqrt(E_m))
```

with `a = A/σ²`, `A` estimated per frame from the mean of the per-symbol peak
energies minus the noise floor. Use `log I0(x) ≈ x − 0.5·log(2πx)` for x > 3
and the series below; do the sum with log-sum-exp. The vendored
`mpdecode_core.c` contains CML's `DemodFSK` with the same mathematics as a
reference for the constants.

Keep the ±5 clamp initially, then re-tune it (pitfall 3: the decoder's
optimum scale will move).

### 3. Iterative demapping (`MERCURY_MFSK_ID`), only after a failed decode

1. Decode as today. If CRC fails:
2. From the LDPC decoder's extrinsic LLRs for the 5 bits of each symbol,
   compute a-priori tone probabilities.
3. Recompute bit LLRs with those priors: `llr_k = log Σ_{m: bit k=0}
   p(m | priors excluding bit k)·L_m − log Σ_{bit k=1} ...` where `L_m` is
   the tone likelihood of item 2.
4. Decode again; up to 3 outer iterations.

This needs the decoder to return extrinsic values (posterior minus channel
LLR): add an output argument to `mfsk_ldpc_decode_ws` or a sibling function.
Cost: 3 more LDPC runs on a failed frame; measure on a Pi 4 against the
4.5 s decode allowance (`D_MFSK_MS`, `carousel.c:1633`).

## Steps

1. Give `utils/mfsk_rx_probe` / the sweep script the tone and impulse
   impairments of 00-bench (or drive MFSK from `rx_bench` through
   `modem_mfsk`).
2. Item 1 with tests; sweep: CW at −10..+20 dB relative to the signal on a
   tone bin, AWGN and `ccir-poor`; the 500 Hz-filter case from the code
   comment; neutrality on a flat floor.
3. Item 2; AWGN and fading curves, both floors; re-tune the clamp.
4. Item 3; curves and Pi 4 timing.
5. Findings appended to `docs/MFSK-PORT.md`.

## Tests

- `tests/modem/test_mfsk.c`: `test_whiten_rejects_carrier` (synthetic
  `fft_in` with a strong constant bin: LLR signs follow the sent bits with
  whitening, not without), `test_whiten_neutral_flat`,
  `test_exact_matches_maxlog_at_high_snr` (signs agree, magnitudes within a
  factor), `test_off_identical` (LLRs bit-identical with all switches off).
- `tests/modem/test_mfsk_modem.c`: round trips still pass with each switch
  on.

## Acceptance

- All off: identical LLRs.
- Whitening: with a carrier 10 dB above the signal on one tone bin, delivery
  at −8 dB SNR3k at least 90 % (stock expected near zero; measure and
  record); flat-floor curves within 0.2 dB of stock.
- Exact metric and iterative demapping: report the dB gain at 50 % and 90 %
  delivery; keep each only if it gains at least 0.3 dB on AWGN or fading
  without loss on the other.

## Out of scope

- The tone patterns (ACK, BREAK, NAV) use their own detector
  (`mfsk_pattern_*`) and are untouched.
- Combining repeats at the floor is spec 04.
