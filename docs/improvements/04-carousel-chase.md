# 04 — Chase combining in the carousel by identical repeats (issue #4)

**Kind:** wire change (negotiated). **Depends on:** the capability scheme in
the handbook; 05 or 11 if their LLR stores exist (share them).
**Switch:** `MERCURY_CAR_REPEAT=1` / `[arq] repeat_combine`, default off, and
used only when both ends announced the capability.

## Goal

Stop discarding the energy of frames that fail their CRC: let the receiver
ask for the same frames again and combine the soft bits of both copies. At
the floor this extends reach below the slowest mode.

## Facts

- `modem/modem.c:2014-2041` (`modem_apply_crc_seed`): while a session seed is
  set, `set_harq(ctx, !seed && !control && harq_enabled())`, so combining is
  off for the whole carousel session ("consecutive carousel frames are
  different codewords").
- The combiner (`freedv_700.c:632-690`) holds **one** retained LLR buffer
  per instance (`harq_llr`, `harq_ncopies`), averages, requires the 90 %
  parity gate (`ldpc_harq_combine_parity_ok`) and CRC16.
- MFSK has its own accumulation (`h->llr_acc` in `modem_mfsk.c`,
  `tests/modem/test_mfsk_harq.c`).
- Sender: `build_round()` (`carousel.c:787`) builds a round's frames into a
  caller-provided `car_frame_t fr[]` and advances per-block state
  (`s->next`, `s->sent`, `round_sent`). Frames are not kept after the
  keydown (`io.keydown`: "frames are only valid during the call").
- Receiver: a round that brought nothing is asked for again once, unscored
  (`carousel.c:2643-2653`, trace "rx round lv=%d unheard: asking again"). The
  sender then builds a **new** round; it is not a bit-identical repeat.
- Measured gain of identical repeats (`docs/HARQ-FINDINGS.md`): DATAC1 AWGN
  +0.2 dB 1/200 → 100/200; DATAC3 AWGN −4.8 dB 6/143 → 72/143; DATAC1 MPP
  about 1 dB 10 % → 45 %.
- POLL has no free bit (handbook, "Wire format constraints").

## Design

### Phase 0: measurement only (no protocol work)

Per rung, on the presets the ladder is tuned on, the delivery of a second
and third identical copy with combining against fresh copies:
`utils/harq_snr_sweep.sh` and `harq_skywave_sweep.sh` already do this for
some modes on codec2's `ch`; port the measurement to `rx_bench` (a
`--copies N` option: N bursts with the same payload through consecutive
stretches of the same channel, combined) so it runs on the Watterson presets
including `itur-mdv`. **NVIS is ISI-limited, not noise-limited; if combining
gains little there, restrict the feature to the rungs and conditions where
the table shows a gain.** Do the same for MFSK and MFSK500.

Output: for each rung, `p1` (single-shot delivery) and `p2` (delivery of the
pair, combined) against SNR. The policy below needs these curves.

### Phase 1: protocol

- **Capability** `CAP_REPEAT` announced at connect.
- **Sender** keeps the frames of its last round (bytes only: up to 15 ×
  `CAR_FRAME_MAX`; allocate once per session) and the per-block state from
  before the round.
- **Repeat request.** A POLL variant meaning "send frames at positions P of
  round R again, verbatim". Encoding proposal (to be reviewed with the
  maintainers): `type = POLL`, the escape rung value 7 in the `rung` field
  as the marker, `n` = number of frames to repeat, `poll id` = the round's
  id, bytes 3-4 = bitmap of the 15 positions, the real rung implied by round
  R. Remaining fields keep their meaning (SNR, base, `hi`). An old peer drops
  a rung-7 frame, which is safe. If spec 15 claims rung 7, use a
  reinterpretation of the `need` bytes keyed by a different marker instead.
- **Sender on a repeat request:** re-emit the stored frames at the named
  positions, bit for bit (same header bytes, same poll id), with the usual
  gaps. Do not touch block state. If it no longer holds round R (it has
  already built a newer round), answer with a normal round instead; the
  receiver detects this from the frames' poll id and resets its store.
- **Receiver store:** per frame position of the current round, the LLR
  vector of the failed packet (`np` positions × coded bits; for QAM16C2 15 ×
  16 200 floats ≈ 1 MB, acceptable; allocate per session for the largest
  rung in use). The modem needs to know the position of each packet in the
  round: the carousel tells it the expected count, and the modem numbers
  packets by arrival slot using the known burst spacing (frame air + gap),
  **not** by counting successes. Reuse the arming of spec 03 if present.
- **Combine:** when a repeat round is expected, the modem combines each
  arriving packet at position p with the stored LLRs of position p (average,
  parity gate, CRC), after first trying the new copy alone. On success clear
  the slot; on failure keep the running sum and count, as the existing code
  does.
- **Resets:** clear the store on a new round id, on any mode change, on
  session end, and when a frame decoded from the repeat carries a different
  poll id than expected.

API sketch: `freedv_harq_slot_*` variants taking a caller-owned buffer and
count, so the store lives in the modem layer rather than in each freedv
instance:

```c
int  freedv_get_last_failed_llrs(struct freedv *f, float *out, int max);     /* shared with spec 05 */
int  freedv_decode_combined(struct freedv *f, const float *sum, int ncopies, uint8_t *out_bytes);
```

### Phase 2: policy (receiver decides; behind the switch)

Ask for a repeat instead of new pieces when the expected number of frames
delivered per second of air is higher. With `p1`, `p2` from phase 0 and `m`
frames failed in the last round of `n`:

- fresh round of `n`: expected deliveries `n·p1`;
- repeat of the `m` failed plus `n−m` fresh: `m·q + (n−m)·p1`, where
  `q = (p2 − p1)/(1 − p1)` is the delivery of a frame given that its first
  copy failed.

Repeat when `q > p1` by a margin, estimated online: `p1` is the rung's
`level_delivery()`; `q` is measured per rung once repeats have been tried
(start optimistic only in the band 0.05 < p1 < 0.6, where
`docs/HARQ-FINDINGS.md` saw `q ≈ 0.45-0.5`).

At the floor (`lv == 0`) there is nothing slower to choose, so repeat
whenever the round delivered nothing and the peer was heard (carrier or
pattern). Each doubling of copies is worth about 3 dB on AWGN.

The repeat interacts with link adaptation: a repeated round should be scored
for the rung as its combined outcome, not as a second independent loss
(`measure_level`, `rx_score`). Decide the scoring rule with the sim.

## Steps

1. Phase 0 tables; go/no-go per rung with the maintainers.
2. Capability scheme (shared prerequisite; see handbook).
3. Spec text in `docs/CAROUSEL-SPEC.md`; TLA+ (`specs/carousel/`) extended
   with the repeat request, including the "sender no longer has round R"
   case and a lost repeat request.
4. Sender retention and repeat emission; receiver request; sim support (the
   sim's channel must model a combined copy: per mode, delivery of a repeat
   given a prior failure = `q`).
5. Modem store and combining by position; switch.
6. Policy; sim matrices (60-100 seeds: `cliff:*`, `fade:*`, `nvis`,
   `awgn:0.25`, `step:*`), then `tests/integration` on `ccir-poor` and at the
   floor.
7. Findings document; on-air trial.

## Tests

- Carousel: a repeat request yields byte-identical frames; a sender without
  round R answers with a fresh round; a lost repeat request is recovered by
  the normal re-poll; mixed-version sessions never send the new frame.
- `tests/test_car_explore` (the exhaustive small-loss explorer) extended to
  include repeat rounds: every session still completes with intact data.
- Modem: position numbering survives a missed burst in the middle of a
  round; a stale slot is never combined with a different codeword (inject a
  mismatched poll id).

## Acceptance

- Capability absent or switch off: wire and behaviour identical to trunk
  (existing tests untouched).
- Sim: no channel loses more than 2 % of throughput; the cliff and fading
  channels gain; integrity 100 % on every seed.
- Floor: with repeats, sessions complete 3 dB below the level where they
  complete today on AWGN (bench and integration).
