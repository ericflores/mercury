# 05 — Second-chance decoding with known bits (issue #5)

**Kind:** receiver only. No wire change.
**Depends on:** 09 (the shared list of decode attempts).
**Switch:** `MERCURY_APRIORI=1` / `[arq] apriori_decode`, default off.

## Goal

When a frame fails its CRC, decode it again with the bits the receiver
already knows pinned, immediately (headers) and later (pieces it has since
learned).

## Facts

- DATA frame layout (`docs/CAROUSEL-ARQ.md`, "Wire formats";
  `build_round()`, `carousel.c:787`; `decode_data()`, `carousel.c:534`):
  byte 0 = frames left in round (4), unopened (1), start rung (3);
  byte 1 = poll id (4), highest block opened mod 16 (4);
  then per segment 4 bytes: block id mod 16 (4), K−1 (7), piece count (6),
  first piece index (8), pad (5), with two flag bits in the last header byte
  (`ctl_deaf`, `tx_pat_anchor`, see the end of `build_round`), then `count`
  pieces of `piece_of(c)` = 23 or 24 bytes with consecutive indices mod 256.
- The sender advances each block's `next` index by the pieces it sends, in
  order, so within a round frame `j+1` continues where frame `j` stopped.
- The first frame of a round may begin with a short segment repeating the
  piece the receiver's stream waits for and the next one (`RESEND_RUN` 2).
- When a block has K pieces the receiver decodes it (`decode_block`,
  `carousel.c:1417`) and can then compute **any** piece index of that block
  with `rs_encode` (`datalink_arq/rs_erasure.c`).
- The carousel is told about decoded frames only (`car_on_frame`,
  `carousel.h:340`). It never hears about a failed one. The modem knows: the
  packet completes with `FREEDV_RX_BIT_ERRORS` in `freedv_700.c`.
- Known bits are pinned in the LDPC input by a large-magnitude LLR, as the
  shortened codes already do (`interldpc.c:175-186`, value −100 for the
  unused data bits). **Do not assume the sign convention: establish it with
  a test** (encode a known payload, demodulate without noise, compare
  `sign(llr[i])` with bit `i`).
- Bit order: the decoder's first `Ndatabitsperpacket` outputs are the payload
  bits, packed to bytes by `freedv_pack` (verify MSB-first in the same test).
  The last two bytes are the CRC16, XORed with the session seed.

## Design

### Modem side

1. In `freedv_700.c`, when a packet fails every attempt, keep its
   de-interleaved `llr_raw` in `f->failed_llr` (one buffer per instance,
   sized like `harq_llr`) and set `f->failed_valid`.
2. API:

```c
int  freedv_failed_llrs_available(struct freedv *f);
/* Decode the retained failed packet again with some payload bytes known.
 * mask[i] != 0 means payload byte i is known and equals known[i].
 * Returns 1 and writes the payload to out if the CRC passes. */
int  freedv_redecode_known(struct freedv *f, const uint8_t *known,
                           const uint8_t *mask, uint8_t *out);
```

   It copies `failed_llr`, overwrites the LLRs of known bits with ±P (P = 20;
   measure whether a softer value is better), runs the shared decode
   function, checks CRC16 with the seed.
3. Backend hooks `failed_available` / `redecode_known`; in `modem.c`, the
   payload worker reports a failed packet upward with a handle and its end
   time, and executes re-decode requests under the instance lock.
4. Retention: the buffer is overwritten by the next failed packet and
   dropped on mode change. Phase 2 extends this to a small per-round store
   (shared with spec 04's per-position store).

### Carousel side

1. New optional io callbacks:

```c
/* A payload frame failed its CRC; position unknown. */
void car_on_frame_failed(car_t *c, uint64_t now, int mode);
/* Ask the modem to decode the last failed frame again. */
bool (*redecode)(void *ctx, const uint8_t *known, const uint8_t *mask,
                 size_t len, uint8_t *out);
```

2. **Phase 1, headers.** On `car_on_frame_failed`, build the predicted
   header for the failed frame:
   - frames-left = (frames left in the last decoded frame of this round) − 1,
     or `asked − 1` if it is the round's first frame;
   - poll id = the round's id; highest block, unopened, start rung = as in
     the last frame decoded from this sender;
   - first segment: block id and K of the block the previous frame ended in
     (or the oldest open block), first index = previous first + previous
     count, count = `pieces_per_frame` when the block has that many left.
   Where the layout is ambiguous (round's first frame with a possible
   resend segment; block boundary inside the frame), produce at most 4
   hypotheses, most likely first. Call `redecode` for each until one passes.
   A decoded result goes through the normal `car_on_frame` path.
3. **Phase 2, learned pieces.** Keep, per failed frame of the current round,
   the predicted layout. When `decode_block` completes a block that a stored
   failed frame carried together with pieces of another, still-open block,
   compute the completed block's pieces, pin them, and re-decode. Frames
   whose every piece is already known are dropped without decoding.
4. **Phase 3, control frames.** The station waiting for a POLL predicts type,
   poll id, rung, window base and `hi`; same mechanism on the control
   decoder. Separate measurement; the false-accept budget is tighter because
   a wrong POLL makes the sender transmit.

## False accepts

Each hypothesis is another chance for a wrong codeword to pass CRC16
(2^-16 each). Rules:

- at most 4 hypotheses per failed frame;
- accept only if the decode **converged** (parity-check count at 100 %, or
  the 90 % gate of the HARQ path, `ldpc_harq_combine_parity_ok`) and CRC16
  passes;
- the decoded header must equal the hypothesis's pinned header (it will, if
  pinned hard) and pass `decode_data`'s range checks;
- the block CRC-32 remains the last line.

## Steps

1. Sign/bit-order test; `freedv_redecode_known`; bench column in `rx_bench`:
   pin a random fraction f of payload bytes (f = 0.05, 0.1, 0.2, 0.5, 0.8) and
   report delivery per mode. This table decides which rungs are worth the
   carousel work (expect DATAC15, DATAC4, MFSK; little for the long codes
   from headers alone).
2. Sim instrumentation: in `tests/sim`, count for failed frames how often the
   header was predictable and how many payload bytes were known later. No
   decoding in the sim (its channel is an erasure model); this measures
   opportunity only.
3. Modem plumbing, carousel phase 1, switch.
4. `tests/integration` at the DATAC15 cliff, A/B.
5. Phase 2; phase 3 only with maintainer agreement.
6. Findings document.

MFSK: the same idea applies to `modem_mfsk.c` (`h->llr_di` is the retained
buffer); add after the OFDM path works.

## Tests

- Modem: `test_redecode_known` (a burst that fails at the cliff decodes when
  20 % of bytes are pinned; wrong pinned bytes never produce a CRC pass in
  1000 seeds).
- Carousel (`tests/sim/test_carousel.c` with a fake `redecode`): header
  prediction is exact for mid-round frames over 1000 random rounds;
  hypotheses never exceed 4; a wrong prediction changes nothing.

## Acceptance

- Off: identical behaviour.
- Bench: report dB gain at 50 % delivery for f = 0.2 on DATAC15 (the header
  case); proceed to carousel work only if it is at least 0.4 dB.
- False accepts: zero in 10 000 wrong-hypothesis decodes at the cliff.
