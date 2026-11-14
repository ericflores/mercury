# 28 — Redundancy in instalments: thin frames and parity top-ups (issue #28)

**Kind:** wire change (negotiated). **Depends on:** the capability scheme
in the handbook; 04 (its sender frame store, its repeat request and its
receiver soft-bit store are reused, not duplicated); 23 (a stop test in
the decoder); 03 or 21 for the gain under the cliff on the narrow rungs.
**Switch:** `MERCURY_INSTALMENTS=1` / `[arq] instalments`, default off, and
used only when both ends announced the capability.

## Goal

Stop sending parity the channel did not need, and stop throwing away
frames that almost decoded: send a frame thin, and add to it only when the
receiver could not decode it.

## Facts

- Measured, code level (`utils/ldpc_ir_sweep`, tables in
  `TRANSMIT-SIDE-RESEARCH.md` section 2): data plus part of the parity
  decodes within 0.35 to 1.1 dB of an ideal code of that rate on all four
  codes in use, from a rate of 0.88 downwards; two copies are 3.0 dB better
  than one; the parity must be left out evenly spread (bit-reversed order),
  not from the end.
- Airtime, DATAC1's code: 1.40 coded bits per payload bit instead of 2.00
  from 3 dB over the cliff; 2.11 instead of 3.48 at the cliff; a smooth
  rate under it.
- The codes are accumulator codes: `encode()` (`mpdecode_core.c:78`)
  computes each parity bit from data bits and the previous parity bit. A
  parity bit left out merges two checks, which is why puncturing is benign.
- The modem already handles codewords that are not sent whole, on the data
  side: `set_data_bits_per_frame()` and `ldpc_mode_specific_setup()`
  (`interldpc.c:64-84`) shorten the code for DATAC4, DATAC16 and DATAC17;
  `ldpc_encode_frame()` (`interldpc.c:88`) and `ldpc_decode_frame()`
  (`interldpc.c:192`) insert the known bits. Leaving out parity is the
  mirror image: zero LLR where nothing was sent.
- A packet is `np` modem frames of `ns - 1` data rows of `nc` carriers
  (`ofdm_mode.c`); its bits are the unique word, the codeword (interleaved
  over the whole packet, `gp_interleaver.c`) and nothing else. DATAC1:
  38 frames × 4 rows × 27 carriers × 2 bits = 8208 = 8192 + 16.
- In a carousel session the combiner is off: `modem_apply_crc_seed()`
  (`modem/modem.c:2065`) calls `set_harq(ctx, !seed && !control && ...)`.
  The combiner itself keeps one soft-bit buffer per instance
  (`harq_llr`, `freedv_700.c:273`) behind a 90 % parity gate
  (`ldpc_harq_combine_parity_ok`, `freedv_700.c:556`).
- The decoder's "done" test counts parity checks from extrinsic signs
  (`SumProductStall`, `mpdecode_core.c:408`, the exit at `:504`). With bits
  missing it never completes, so a thin frame's decode always runs to
  `max_iter`. It also fails to complete on up to 15 % of whole frames near
  a cliff whose bits are all right (measured; research note 2.5).
- Acquisition under the cliff (measured, stock, white noise): DATAC1 to
  −4 dB, DATAC17 to −4 dB, QAM16C2 to +4 dB; the narrow rungs are
  acquisition-limited at their cliffs.
- Frame sizes must be pairwise distinct across modes where code maps a
  payload length to a mode (specs 14 and 15 list the places). A thin
  variant carries the **same payload bytes** as its parent, so it cannot be
  told from it by size: it needs its own mode identity.
- The rung is a 3-bit field with one value free; the POLL has no free bit
  (handbook, "Wire format constraints").

## Design

### Phase 0: through the whole modem, no protocol

1. **Stop test.** In `SumProductStall`, after the hard decisions of an
   iteration, compute the syndrome of `DecodedBits` and stop when it is
   zero. Behind `ldpc->early_stop`-style field of its own (`hd_stop`), off
   by default; unit test: same bits out, fewer iterations, on whole and on
   punctured codewords. This is a receiver-only change and can land with
   proposal 23 first.
2. **Bit order.** One function, used by both ends, that lists the
   codeword's bit positions in sending order: the data bits, then the
   parity bits in bit-reversed order (as `bitrev_order()` in
   `utils/ldpc_ir_sweep.c`), then every bit again in bit-reversed order.
   A unit test pins the first values for each code.
3. **Thin variants as custom configurations.** A thin packet is the
   parent mode with a smaller `np` and a `sent_bits` count: the transmitter
   maps the first `sent_bits` positions of the order into the packet; the
   receiver fills the codeword's LLR vector from them and leaves the rest
   at zero. For DATAC1: 27 frames (5816 bits, 71 %) and 23 frames
   (4952 bits, 60 %); the same fractions for DATAC17 and QAM16C2, rounded
   to whole modem frames. Opened through `freedv_open_advanced` as
   `rx_bench` opens its variants.
4. **Bench.** `rx_bench` gets `RX_BENCH_THIN=<np>`: delivery of each thin
   variant against the whole frame on white noise, CCIR Good, Moderate,
   Poor and the NVIS preset, with the stock receiver and with 09 + 19
   (+ 26). Then `--topup`: a failed thin frame followed, after the usual
   gap and through the next stretch of the same channel, by a burst with
   the bits not sent yet, combined.
   **Stop here if** the thin frames lose more than 1 dB against the
   code-level tables on white noise, or if under fading a thin frame plus
   its top-up delivers less per second of airtime than whole frames do.

### Phase 1: thin rungs

- Capability `CAP_THIN`.
- The receiver asks for the thin variant of a rung when its margin over
  that rung's threshold is at least the measured step (from phase 0, per
  rung), with the ladder's existing hysteresis. Encoding: to be chosen with
  the maintainers; the candidates are the escape rung value 7 followed by
  the real rung in a reinterpreted field (as spec 04 proposes for its
  repeat request) or one thin variant per wide rung selected by the
  `short_rung` bit when the capability is on. Spec 15 also wants value 7:
  reconcile first.
- A failed thin frame is an erasure, exactly as a failed frame is today.
  No store, no top-up yet. This phase alone gives the airtime saving over
  the cliff.
- Timing: rows in `arq_mode_table` (`arq_protocol.c:61`) for the thin
  variants; a pool slot each (`pool_modes[]`, `modem/modem.c:723`) or one
  decoder re-bound per round, whichever the pool allows.

### Phase 2: top-ups

- On top of spec 04's machinery: the sender keeps its last round's frames;
  the receiver keeps, per frame position of the round, the LLR vector of a
  frame that failed; the repeat request names positions.
- A top-up request adds a level: "send me the next part of the frames at
  positions P". The sender answers with bursts that carry, for each named
  frame, the next `sent_bits` positions of the order after those already
  sent (first the rest of the parity, then the repeat cycle). A top-up
  packet has the unique word and coded bits only; it has no CRC of its
  own, and the modem checks the CRC of the frame it completes after
  combining.
- The receiver adds the top-up's LLRs into the stored vector at the
  positions the order gives and decodes. The parity gate of the combiner
  applies; a stored vector is dropped when its round is superseded (spec
  04's rule).
- Under the cliff a top-up has to be received without a strong preamble
  detection. Where proposal 03 exists, arm the decoder for the expected
  arrival; without it, restrict top-ups to the rungs and levels where
  acquisition holds (table above).

## Tests

- Unit: the bit order; encode a thin packet, decode it noiseless; a thin
  packet plus its top-up equals the whole codeword's LLRs; with the switch
  off, transmitted samples and decoded bytes are identical to trunk.
- Unit: mixed versions at connect (a peer without `CAP_THIN` gets today's
  frames).
- Bench: phase 0's tables.
- Simulation (`tests/sim`): goodput with and without, across the sim
  matrix the ladder constants were tuned on; no constant in `carousel.c`
  changes without it.
- Integration: sessions over the Watterson presets with the capability on
  both ends, one end only, and off.
- False accepts: a top-up combined with the wrong stored frame must fail
  the CRC; the noise-only and wrong-session controls of `rx_bench`.

## Acceptance

- Phase 1: at 3 dB or more over a wide rung's threshold, bytes delivered
  per second of transmission rise by at least 20 % on white noise and do
  not fall on any preset.
- Phase 2: between a wide rung's cliff and 3 dB under it, the rung
  delivers where it delivers nothing today, and the session's goodput is
  not lower than stepping down a rung.
- With the switch off, or against a peer without the capability, nothing
  changes.

## Not in scope

A new code family; the narrow rungs under their cliffs (acquisition
first); the control frames (compare with proposal 14 before touching
them).
