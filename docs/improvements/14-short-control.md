# 14 — Short control frames on strong links (issue #14)

**Kind:** wire change (negotiated). **Depends on:** the capability scheme.
**Switch:** `MERCURY_FAST_CTL=1` / `[arq] fast_control`, default off; used
only when both ends announced it.

## Goal

Cut the fixed cost of a round on strong links by sending polls in a short
mode, falling back to DATAC16 on any doubt.

## Facts

- `ROUND_OVERHEAD_MS = 4400 + GUARD_MS + ISS_GUARD_MS + HEAD_MS + TAIL_MS`
  (`carousel.c:1118`) = 4400 + 700 + 900 + 110 + 200 = 6310 ms.
  `ARQ_CHANNEL_GUARD_MS` 700 and `ARQ_ISS_POST_ACK_GUARD_MS` 900 are in
  `arq_protocol.h:129,138`.
- Control frames are 14 bytes (`CAR_POLL_BYTES`) in `ARQ_CONTROL_MODE`
  (DATAC16, 3.74 s on the air). `put_ctl()` (`carousel.c:475`) chooses
  between DATAC16 and the MFSK floor (control-deaf peers).
- DATAC0 exists in `ofdm_mode.c` (9 carriers, about 500 Hz, `H_128_256_5`,
  14 payload bytes, 0.44 s + preamble/postamble; upstream: 70/100 at 0 dB on
  MPP). It has **no pool slot** (`pool_modes[]`, `modem.c:676-681`) and no
  row in `arq_mode_table` (`arq_protocol.c:57-76`).
- Modes are told apart by frame size in places; DATAC13 was removed from the
  table because its 14 bytes collided with DATAC16's (comment at
  `arq_protocol.c:53-55`). DATAC0 has the same 14 bytes: find every place
  that maps a frame length to a mode before adding it
  (`grep -n "payload_bytes" datalink_arq/*.c modem/modem.c`).
- Each end already tells the other how well it hears it: the 6-bit SNR in
  every control frame (`take_peer_snr`, `carousel.c:1253`).
- A poll with data of the poller's own already rides inside a data frame
  (`inline_poll`, `carousel.c:381`).
- The receive side runs a control worker (DATAC16) and a payload worker
  bound by `bind_rx`. A station that is only sending has an idle payload
  decoder between its rounds.

## Design

1. **Mode.** Add DATAC0 to the pool and the timing table as the "fast
   control mode" (`ARQ_FAST_CONTROL_MODE`). Measure its real frame duration
   on the bench like the other rows.
2. **When the poller uses it** (all must hold):
   - capability on both ends;
   - the peer reports hearing this station at ≥ `FAST_CTL_MIN_DB` (start at
     +8 dB; DATAC0's own threshold is near 0 dB, so this is a wide margin)
     in its last two control or data frames;
   - the last fast poll, if any, was answered;
   - not control-deaf, not BW500 (DATAC0 is 500 Hz wide but a BW500 station's
     filter passband and floor rules differ; leave it out at first).
3. **Who listens.** The station expecting a poll binds its **payload**
   decoder to DATAC0 whenever it is not expecting data frames itself: after
   its own round's keydown ends, until it next expects a round. The control
   decoder stays on DATAC16, so nothing that works today can be missed. Add
   this to the carousel as a second `bind_rx` target chosen in the sender
   states (`s_*` functions), and make sure `bind_rx` is called again before
   any data is expected.
4. **Fallback.** A fast poll that brings no round is repeated in DATAC16 by
   the existing unheard-round path (`carousel.c:2643`); set a flag so the
   next 8 polls use DATAC16 (back off), doubling on each further failure.
5. **Scoring.** `round_overhead()` (`carousel.c:1124`) must return the
   shorter overhead when the fast mode is in use, so goodput comparisons
   between rungs stay right (short rounds become cheaper, which changes
   `level_goodput`).
6. **Timing windows.** Every place that assumes a control frame lasts
   `ctl_air_max()` (`carousel.c:1669`) and the slot arithmetic around
   `s_ctl_ms`, `D_OFDM_MS` must take the mode actually sent. The TLA+ model
   in `specs/turns/` covers these slots; update it.

### Separate switch: per-session guards (`MERCURY_ADAPT_GUARDS`)

Measure, per session, (a) how long after this station's unkey the peer's
first sync appears, and (b) from the peer's reports or timing, how late this
station's frames are heard. Shrink `GUARD_MS` / `ISS_GUARD_MS` towards the
measured values plus a margin (never below 250 ms), per session. This is
independent of the fast control mode and can ship first or never; it needs
two real radio types to judge.

## Expected gain (arithmetic)

Fast poll about 1.1 s instead of 3.74 s: 2.6 s per round. Full QAM16C2
round: 37.3 s → 34.7 s (+7.5 % throughput). One DATAC1 frame per round:
11.1 s → 8.5 s (+31 %, mostly a latency gain for interactive traffic).

## Steps

1. Sim estimate without wire work: a build-time flag that makes
   `ROUND_OVERHEAD_MS` 2600 ms shorter at SNR above the threshold, run on the
   8 KB matrix and the chat/keyboard scripts (`CAR_CHAT`, `CAR_KBD` in
   `tests/sim/carousel_sim.c`). If the gain is under 3 % overall, stop here
   and record why.
2. Capability scheme.
3. DATAC0 in pool and table; frame-size disambiguation audit; delivery curve
   and false-sync rate of DATAC0 with `acquire_vs_decode` (it has never been
   an ARQ mode here).
4. Carousel: mode choice, listener binding, fallback, overhead accounting;
   spec and TLA+.
5. Sim matrices, including a channel that loses DATAC0 but not DATAC16
   (SNR between their thresholds) to exercise the fallback.
6. `tests/integration` at +15 dB and near the threshold; findings document.

## Tests

- Mixed-version: a station without the capability never receives DATAC0.
- A lost fast poll costs one round and triggers the back-off.
- The listener is always bound to the round's mode by the time data arrives
  (explorer test over all single-loss cases, `tests/test_car_explore`).
- Overruns: no keydown overlaps the peer's (the sim's collision counter
  stays zero).

## Acceptance

- Capability absent or switch off: identical to trunk.
- Sim: clean and high-SNR fading channels faster; no channel more than 1 %
  slower; zero collisions; integrity 100 %.

## A code candidate from modem73

modem73 sends 32 bytes in 0.54 s on a (1024,288) polar code with list
decoding (`MODEM73-NOTES.md`). Before fixing the short frame's code, bench a
CRC-aided polar code (N = 512 or 1024, list 8-32) against the short LDPC
code decoded with proposal 12's ordered-statistics decoder, at equal payload
and airtime. Short blocks are where list decoding earns its cost.
