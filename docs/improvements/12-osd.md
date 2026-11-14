# 12 — A second decoder for the short codes (issue #12)

**Kind:** receiver only. No wire change. **Depends on:** nothing (uses the
attempt list of 09 if present).
**Switch:** `MERCURY_OSD=1` / `[audio] rx_osd`, default off.

## Goal

Decode control frames (and DATAC15) that belief propagation gives up on,
with ordered-statistics decoding (OSD), without raising the false-accept
rate.

## Facts

- Decoder: `SumProduct()` in `modem/freedv/mpdecode_core.c` via
  `run_ldpc_decoder()` / `ldpc_decode_frame()` (`interldpc.c:155`); up to
  `max_iter` = 100.
- Codes: DATAC16 and DATAC15 use `H_256_768_22` (`modem/freedv/H_256_768_22.c`,
  mother code (768, 256), parity-check matrix as `H_rows`/`H_cols`).
  DATAC15 uses it whole (256 information bits = 30 payload bytes + CRC16);
  DATAC16 is shortened to (640, 128): the 128 unused information bits are
  known and pinned (`LDPC_PROT_2020` branch).
- A frame's 16 CRC bits are part of the information bits.
- The control plane is the weakest link by the project's own figures:
  DATAC16 stops near −10 dB, patterns are detected to about −14 dB.

## Algorithm (OSD of order w, run only after BP fails)

Input: the channel LLRs of the N coded bits (for a shortened code, drop the
known positions first so the working code is (640, 128)).

1. Build the systematic generator `G` (K × N) of the working code once per
   mode, from `H` (Gaussian elimination over GF(2), bit-packed in 64-bit
   words). Cache it.
2. Sort positions by |LLR|, most reliable first.
3. Permute `G`'s columns in that order and reduce it to find the K most
   reliable **independent** positions (the most reliable basis, MRB).
4. Order-0 candidate: hard decisions on the MRB, re-encode.
5. Order-1 and order-2: flip each single MRB bit and each pair, re-encode
   (incrementally: XOR one or two rows of the reduced `G`), and compute the
   soft distance `D = Σ |LLR_i|` over positions where the candidate
   disagrees with the hard decision of the channel.
6. Keep the best and second-best `D`.

Complexity for K = 128: 1 + 128 + 8128 candidates × 640 bits, bit-packed:
well under 10 ms on a desktop; measure on a Pi 4. For K = 256: 32 897
candidates × 768 bits; restrict order 2 to pairs among the 64 least reliable
MRB positions if needed.

## Acceptance gate (the important part)

OSD always returns a valid codeword, so "parity checks satisfied" says
nothing. Accept the winner only if all hold:

1. `D_best < T1 · Σ|LLR|` (the winner is close to what was received);
2. `D_second − D_best > T2 · D_best` (it is clearly better than the runner-up);
3. CRC16 passes (with the session seed where one applies);
4. for control frames, the existing range checks in `decode_ctl`.

`T1` and `T2` are set from a noise-only run and a wrong-session run so the
measured false-accept rate stays at or below today's (which is the CRC16
escape rate of frames BP happens to converge on). Report the false accepts
per million windows in the findings.

## Where it goes

- New file `modem/freedv/osd.{c,h}` (Mercury code, not vendored), added to
  `OBJS_COMMON` in `modem/freedv/Makefile`:

```c
typedef struct osd osd_t;
osd_t *osd_create(const struct LDPC *ldpc, int n_known_info_bits);
/* llr: N channel LLRs of the working code. Returns 1 if a candidate passed
 * the distance gates; out receives the K information bits. */
int osd_decode(osd_t *o, const float *llr, int order, uint8_t *out,
               float *d_best, float *d_second);
void osd_destroy(osd_t *o);
```

- `freedv_700.c`: one more attempt after the others, only for instances
  whose working K ≤ 256 and only when enabled.
  `freedv_set_osd(f, order)`; backend hook; switch.

## Perturbed re-decoding for the long codes (phase 2)

For K > 256: on BP failure, take the 8 least reliable bits, and for each of
up to 16 sign patterns of them, saturate those LLRs to ±P and run BP again
(early exit on CRC pass). No size limit; expect a smaller gain. Keep it
behind its own switch value (`MERCURY_OSD=2` enables both).

## Steps

1. Offline first: dump the LLRs of BP-failed DATAC16 frames from a bench run
   at −10..−13 dB (a `--dump-llr` option in `rx_bench`) and run OSD on them
   in a standalone test program. This gives the achievable gain and the
   distance statistics for the gate before any modem change.
2. `osd.{c,h}` with unit tests.
3. Gate thresholds from the noise-only and wrong-seed controls.
4. Modem integration, switch.
5. Bench: DATAC16 and DATAC15, AWGN and `ccir-poor`, with the stock
   acquisition and, if available, with spec 03 (control frames are
   acquisition-limited too; the two are complementary).
6. Pi 4 timing; findings document.

## Tests (`tests/modem/test_osd.c`)

- `test_generator_matches_encoder`: `G` re-encodes random payloads to the
  same codeword as `ldpc_encode_frame`.
- `test_osd_decodes_clean`: no noise, order 0 succeeds.
- `test_osd_beats_bp_at_cliff`: seeded noisy codewords where BP fails; OSD-2
  recovers a pinned minimum number.
- `test_gate_rejects_noise`: 10 000 noise-only LLR vectors, zero accepted.
- `test_shortened_code`: the (640, 128) working code round-trips.

## Acceptance

- Off: identical behaviour.
- DATAC16 AWGN: delivered/acquired at −12 dB improves by a measured amount
  (target: at least half of the BP failures among acquired frames recovered).
- False accepts: zero in 10^5 noise-only windows and 10^5 wrong-seed frames.
- Pi 4: worst-case OSD time under 150 ms for DATAC16.
