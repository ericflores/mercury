# 20 — Decision-directed channel estimation (issue #20)

**Kind:** receiver only. No wire change.
**Depends on:** 09, 19 (the attempt list and the alternative estimate), 23
(time budget), 00-bench.
**Switch:** `MERCURY_DD` / `[audio] rx_decision_directed`, default off.

## Goal

After a failed decode, use what the decoder learned about the data symbols
as extra channel references, estimate again, decode again. Target: a useful
part of the measured genie ceiling (1.0-1.35 dB on white noise, about 2 dB
under CCIR Poor, 2-3× the frames on disturbed NVIS; research note 3.2).

## Facts

- A packet is `np` modem frames; each is one pilot row and `ns − 1` = 4 data
  rows of `nc` carriers. Pilots are 1/5 of the symbols.
- The LDPC decoder (`SumProduct()`, `modem/freedv/mpdecode_core.c:365`)
  computes for every coded bit, on every iteration, the posterior `Qi`
  (initial value plus all incoming check messages) and then takes its sign
  (`Qi < 0` means bit 1). It returns only the hard bits. `ssum` is the count
  of satisfied parity checks on that iteration.
- Symbols reach the decoder through
  `ofdm_disassemble_psk_modem_packet_with_text_amps()` (removes the
  unique-word symbols) and `gp_deinterleave_comp()`; the inverse operations
  exist for the transmitter (`ofdm_assemble_psk_modem_packet`,
  `gp_interleave_*`).
- Mapping: `qpsk_mod()` / `qam16_mod()` in `ofdm.c`, constellations
  `qpsk[]`, `qam16[]`; note the bit order reversal in `ofdm_mod()`
  (`bits[bps − 1 − i]`).
- The demodulator holds only one modem frame's rows at a time
  (`ofdm->rx_sym`); the packet-level buffers (`f->rx_syms`, `f->rx_amps`)
  hold symbols already phase-corrected with the stock estimate.
- The genie in `prototype/lab2/lab2-prototype.diff` (search `mx_genie`) is
  this proposal with the true symbols in place of the estimated ones:

```c
num = Σ pilots  rx · conj(pilot)   +   Σ data rows  rx · conj(s)
den = (number of pilots used)      +   Σ data rows  |s|²
g[i] = num / den          /* then proposal 09's smoothing across carriers */
```

- Observation from the genie runs: adding the previous and future frames'
  pilots made the result worse (−2.5 dB: 41 against 54 of 60). The frequency
  tracking loop (`foff_est_hz`, updated every frame from the pilots,
  `ofdm.c` "est freq err based on all carriers") is the suspect: its jitter
  at low SNR rotates one frame against the next.

## Design

### Data kept per packet

In `struct freedv`: `rx_grid`, the raw received rows of the packet before
any phase correction: `np × ns` rows × `nc + 2` carriers, filled as each
modem frame is demodulated (rows 1..ns of `ofdm->rx_sym` are this frame's
pilot and data rows), plus the pilot row that follows the last frame. Also
per frame the frequency estimate used for it, so later passes can de-rotate
consistently. With recycling on (proposal 19) keep the recycled rows.

### Decoder output

`run_ldpc_decoder()` / `SumProduct()` gain an optional `float *posterior`
(length `CodeLength`), written from `Qi` on the last iteration. Stock
callers pass NULL.

### One refinement round

1. Posterior LLRs of the coded bits (payload order) → re-interleave
   (`gp_interleave_float`-equivalent) and re-insert the known unique-word
   bits at their positions (as known symbols with full weight).
2. Soft symbol for each data position: the expectation of the constellation
   point under the bit probabilities, `ŝ = Σ_points P(point) · point`, with
   `P(bit = 1) = 1 / (1 + exp(L))`. For QPSK that is two `tanh` terms. A
   symbol the decoder is unsure of has `|ŝ|` near 0 and so carries little
   weight in the next step: no threshold is needed.
3. Per modem frame, per carrier: `g[i] = num / den` as above with `ŝ`, then
   the smoothing across carriers of proposal 09 with the phase slope
   removed.
4. Correct the frame's data rows with `g`, form symbols and amplitudes,
   then the shared "symbols to decoded bits" function.
5. Parity gate and CRC as for every fallback attempt.

Repeat up to `DD_ROUNDS` (start with 2) while the count of satisfied parity
checks rises from round to round; stop otherwise.

Do **not** feed a round's own channel LLRs back as the next round's prior:
only the channel estimate changes between rounds, and each decode starts
from fresh channel LLRs. That keeps the loop from confirming its own errors.

### Frame-to-frame phase

Before anything else, measure the suspect: on the bench, log per frame the
common phase of the pilots after the stock correction. If it wanders by
tens of degrees between frames at low SNR, add a per-frame common-phase
estimate (from pilots and soft symbols) to step 3 and allow averaging
across neighbouring frames only after removing it. Proposal 09's `t4`
weights should be re-measured afterwards; they may gain as well.

### Second phase: NVIS

With soft symbols for the whole packet, the tail of each symbol through the
estimated channel can be reconstructed and subtracted from the next
symbol's window, and the lost cyclic part restored (the RISIC family; see
the research note, section 4). It needs the channel's impulse response,
from an inverse transform of `g` across carriers. Do this only after phase 1
is measured; it is the one known route to the rest of the NVIS loss.

## Steps

1. `rx_bench`: the genie as a permanent bench-only variant (it is the
   yardstick). Needs a hook that hands the receiver the transmitted grid;
   keep it out of the production path (`#ifdef MERCURY_BENCH_GENIE`).
2. Per-frame phase measurement and, if confirmed, the common-phase fix.
3. `rx_grid`, posterior output, soft symbols, one round; tests.
4. Rounds, early exit, switch.
5. Bench matrix: all rungs; AWGN, `ccir-poor`, `ccir-moderate`, `itur-mdv`,
   `ccir-flutter`; columns stock, 09 + 19, + DD, genie.
6. Timing per round on a Pi 4 and a desktop (proposal 23).
7. Findings document. Then decide on phase 2.

## Tests

- Mapping round trip: bits → symbols → hard LLRs → soft symbols equals the
  symbols; interleave/de-interleave and unique-word insertion are inverses
  (this is where sign and order bugs hide; pin it first).
- With perfect LLRs (large magnitudes, correct signs) one DD round
  reproduces the genie's estimate.
- `test_off_is_identical`.
- Seeded cliff test: frames added, none lost.
- A packet of pure noise never passes (gate and CRC), 1000 seeds.

## Acceptance

- Off: identical to trunk.
- On: delivered ≥ without it on every cell.
- White noise: at least half of the gap between "09 + 19" and the genie
  closed at the 50 % point, for DATAC1 and DATAC17.
- CCIR Poor and NVIS: report the fraction of the genie's extra frames
  recovered; keep the feature if it is above a third on either.
- False accepts: zero in 1000 noise-only windows per mode.
