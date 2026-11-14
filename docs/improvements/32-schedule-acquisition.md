# 32 — Acquisition that uses what the protocol knows (issue #32)

**Kind:** receiver side first (modem and ARQ inside one station), no wire
change for the search over a round; copies need 04. **Depends on:** 03
(schedule-aided acquisition: this generalises it), 00-bench.
**Switch:** `MERCURY_SCHED_ACQ=1` / `[audio] rx_sched_acq`, default off.

## Goal

At the floor, finding the signal is what fails first. Search for the frames
the receiver asked for with everything it knows about them: where in time,
how many, how far apart, at what offset; integrate over all of them; then
decode each at the timing found.

## Facts

- "Delivered == acquired" at the floor: `docs/MFSK-PORT.md` for the MFSK
  floor; for the OFDM rungs, `rx_bench` shows DATAC3 with 09 + 19 at
  −5.0 dB acquiring 37 of 100 and delivering 35, at −5.5 dB 11 and 9
  (`docs/DECISION-DIRECTED-FINDINGS.md`, white noise table).
- The principle measured on the deep floor (`BELOW-NOISE-RESEARCH.md`,
  section 4.4): the search added up over the copies at the expected spacing
  finds 40 of 40 down to −24 dB with 4 copies and to −28 dB with 8, where a
  copy alone is found 9 and 0-2 times.
- What the receiver knows when it polls: the rung and the number of frames
  it asked for (`build_round()` on the sender, `datalink_arq/carousel.c:787`;
  the receiver's POLL fields, handbook "Wire format constraints"), the gap
  between bursts (`burst_gap_ms()`, `carousel.c:192`), the mode's air time
  (`arq_mode_table`), its own keydown end, and the peer's frequency offset
  from earlier frames.
- OFDM acquisition today: `ofdm_sync_search()` / `est_timing_and_freq()`
  (`modem/freedv/ofdm.c:1366`, `:1553`) on each burst alone, the state
  machine `ofdm_sync_state_machine_data_burst()`.
- The modem already records what the decoders were handed when asked
  (`rx_dump_chunk`, `MERCURY_RX_DUMP`, `modem/modem.c:837`); a ring of the
  last minutes is the same thing in memory (8 kHz × 16 bit × 10 min =
  9.6 MB).
- Proposal 03 uses the schedule to arm acquisition at the right moment; this
  proposal also uses it to integrate over the round and to search after the
  fact.

## Design

### Phase 0: how much it is worth (bench)

1. `rx_bench`: a token that hands the decoder the true timing and offset
   (the bench knows them), to measure each narrow rung's decoding limit
   apart from its acquisition. If DATAC3, DATAC4, DATAC15 and DATAC16 do
   not decode below today's acquisition limit, the OFDM half of this
   proposal is worth nothing and only the floor's (31) remains.
2. A round of N frames of a rung at its known spacing; a search that adds
   up the preamble correlation of every frame at a common start time and
   offset; detection and false-alarm rates against one-by-one acquisition,
   N = 1, 2, 4, 8. Use the bounded metric (one loud frame cannot decide).

### Phase 1: the search over a round

- The ARQ tells the modem, when it has polled: mode, n, spacing, the window
  in which the first frame can start, the expected offset.
- The modem keeps a ring of recent audio. When the window closes with fewer
  frames acquired than asked for, it searches the stored audio of the
  window with the added-up statistic, and hands each decoder the audio of
  each frame with the timing and offset found (a forced acquisition: a
  small addition to the OFDM and MFSK receivers).
- Frames found this way go through the same parity gate and CRC.

### Phase 2: copies

With 04 / 28 / 31: when the same frames come again, the search adds up the
statistics of every copy, and the decoder combines their soft values.

## Tests

- Unit: the added-up statistic over a synthetic round; forced acquisition
  decodes a burst at a given timing; with the switch off nothing changes.
- Bench: phase 0; false accepts (noise only, other sessions' frames, a
  round that was never sent).
- Integration: sessions at the narrow rungs' cliffs, stock against on.

## Acceptance

- Phase 0 shows the narrow rungs decoding at least 1 dB below their
  acquisition limit, or this phase stops (and the note says so).
- Phase 1: at a narrow rung's cliff, more frames delivered per round, no
  false accept in the controls, no session slower.

## Not in scope

Changing the preambles; the deep floor's waveform (31).
