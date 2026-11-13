# 13 — Multi-packet bursts on the fast rungs (issue #13)

**Kind:** wire change (negotiated). **Depends on:** the capability scheme;
benefits from 03.
**Switch:** `MERCURY_MULTI_BURST=1` / `[arq] multi_packet_bursts`, default
off; used only when both ends announced it.

## Goal

On rungs and links where rounds arrive clean, send a round's N frames behind
one preamble instead of N, saving preamble, postamble and gap per frame.

## Facts

- Today: `build_round()` gives each frame `gap_ms = burst_gap_ms()` (100, or
  200 for QAM16C2/MFSK; `carousel.c:192`), and `modem.c` (1648-1672) emits
  each frame as its own burst via `append_burst_audio(..., frames = 1, ...)`.
- `append_burst_audio()` already takes a `frames` count (preamble, `frames`
  packets with CRC, postamble), and `send_modulated_data()` takes
  `frames_per_burst`.
- The decoder: `ofdm_sync_state_machine_data_burst()` (`ofdm.c:2401-2460`)
  stays `synced` across packets and returns to `search` after
  `packetsperburst` packets (`ofdm.c:2446`). `freedv_set_frames_per_burst()`
  sets it; `modem.c` configures every instance with 1.
- `utils/burst_train_probe.c` measured separate bursts: with no gap the
  decoder misses bursts after the first; 100 ms is enough on AWGN, QAM16C2
  needs 200 ms under fading. That result is about re-acquisition and says
  nothing about one continuous multi-packet burst.
- Timing constants: QAM16C2 packet about 3.1 s (31 modem frames of 100 ms),
  burst 3.30 s, table value 3.70 s per frame (`arq_protocol.c`), 8 frames
  per 30 s keydown (`MAX_KEYDOWN_MS`), round overhead 6.31 s.
- HARQ notes in `docs/HARQ-FINDINGS.md` ("Within-burst hazard") describe what
  multi-frame bursts did to the old combiner; relevant if spec 04 lands.

## Design

1. **Capability** `CAP_MULTI_BURST`.
2. **Request.** The receiver decides per round. It needs one bit in the POLL
   ("as one burst"). No bit is free (handbook): options are a
   reinterpretation agreed in the capability (for example: on rungs ≥ 4, `n`
   frames are always one burst once the capability is on and the receiver
   has signalled "clean" through an existing field), or the escape rung.
   Simplest robust rule to start with: **with the capability on, rounds on
   rungs 4-6 of more than one frame are single bursts when the round's poll
   id is odd**, and the receiver picks the parity of the id it uses. Poll
   ids are 4 bits and chosen by the receiver; check in `carousel.c` that
   nothing else depends on their sequence before relying on this, otherwise
   use the escape value.
3. **Sender.** For such a round, emit one burst: preamble, the N packets,
   postamble. In `modem.c`'s keydown builder, group consecutive frames of the
   same mode flagged "joined" and call `append_burst_audio` once with
   `frames = N` on a transmit instance configured for N. The frames' bytes
   are unchanged (each still has its own header and CRC16).
4. **Receiver.** Bind the payload decoder with `frames_per_burst = N` for
   that round (extend `bind_rx` or the `expect_round` callback of spec 03).
   After the round, back to 1.
5. **Accounting.** `round_air()` (`carousel.c:385`) and `keydown_cap()` must
   use the joined length: `preamble + N·packet + postamble` instead of
   `N·frame + (N−1)·gap`. Add a `mode_packet_air()` to the timing table
   (measured) so more frames fit in the 30 s keydown.
6. **When the receiver asks for it:** rung ≥ 4, the last two rounds on this
   rung lost no frame, SNR at least 3 dB above the rung's threshold. Any
   loss in a joined round → next round separate.
7. **Loss semantics.** If the preamble is missed, the whole round is lost
   except possibly the last packet (postamble acquisition rewinds one
   packet). Score such a round as one acquisition loss for adaptation
   purposes, not as N independent frame losses, or the dead verdict
   (`DEAD_RUN` 4) will fire on a single missed preamble. Work this out in the
   sim before touching `measure_level`.

## Phase 0: measurement (decides everything)

Extend `utils/burst_train_probe.c`: third receiver "joined" = one N-packet
burst through the same channel stretch as the N-burst train. Report frames
delivered and **frames delivered per second of air** for N = 2, 4, 8:

- AWGN at the rung's cliff + 3 dB and + 10 dB;
- `ccir-poor`, `ccir-moderate` at + 6 and + 12 dB over the threshold;
- a slow deep fade (`ccir-good`, 0.1 Hz) where a whole burst can sit in a
  fade.

Also measure what the 0.4 s between the on-air burst length (3.30 s) and the
table's 3.70 s consists of (head/tail silence per keydown, or per burst?);
the achievable gain is 9 % if it is per keydown and up to 20 % if per burst.

Proceed only if "joined" wins in frames per second on at least the AWGN and
moderate cases without losing more than it gains under `ccir-poor`.

## Steps

1. Phase 0 and a short findings note.
2. Capability scheme; spec text; TLA+ (`specs/carousel/`, `specs/turns/`
   for the slot lengths).
3. Sender grouping in `modem.c`; receiver binding with N; accounting.
4. Sim: channel model with a correlated "missed preamble" loss for joined
   rounds, parameterised from phase 0; matrices.
5. `tests/integration` at +15 and +25 dB, AWGN and `ccir-moderate`.
6. Findings document; on-air trial.

## Tests

- Joined bursts decode in a noiseless loopback for N = 2..15 on DATAC1,
  DATAC17, QAM16C2 (`tests/modem/`).
- Keydown length never exceeds `MAX_KEYDOWN_MS` (and the per-station
  `max_keydown_ms`) with the new accounting.
- Mixed-version sessions never see a joined burst.
- A missed joined round falls back to separate bursts on the next round.

## Acceptance

- Capability absent or switch off: identical audio and behaviour.
- High-SNR sim and integration: throughput up by the amount phase 0
  predicted (±3 %); fading sim channels not more than 1 % slower; integrity
  100 %.
