# 16 — Receiver-chosen sub-band for the narrow rungs (issue #16)

**Kind:** wire change, research priority. **Depends on:** 10 (gap noise
spectrum), 07 (to know what weighting alone can rescue), the capability
scheme.
**Switch:** `MERCURY_SUBBAND=1` / `[arq] subband`, default off.

## Goal

Let a narrow mode sit where the passband is clean instead of always at
1500 Hz.

## Facts

- All modes are centred on 1500 Hz (`config->tx_centre`, `rx_centre`,
  `ofdm_mode.c:31-32`). Occupied widths: DATAC15/16 187 Hz (3 carriers),
  DATAC4 250 Hz (4), DATAC3 562 Hz (9), MFSK 1000 Hz, MFSK500 500 Hz.
- The carrier frequencies are derived from the centre when the OFDM instance
  is created (`ofdm_create`, look for `tx_centre`, `rx_centre`, `tx_nlower`,
  `rx_nlower`, `doc`); there is no setter to move an open instance. A custom
  configuration can be opened through `freedv_open_advanced` with
  `FREEDV_MODE_DATA_CUSTOM` (`freedv_700.c:205-209`), or a setter can be
  added that recomputes the carrier table and filters.
- TX and RX band-pass filters (`tx_bpf_proto`, `rx_bpf_en`) are designed
  around the centre.
- BW500 sessions may sit behind a 500 Hz filter: excluded.

## Phase 0: is it needed? (bench only)

With the impairments of 00-bench, for DATAC15, DATAC4 and DATAC3 at +6 dB
over their thresholds:

1. a CW carrier and an FSK signal on 1500 Hz at 0, +10, +20 dB relative to
   the signal: stock delivery;
2. the same with spec 07's per-carrier weights;
3. the same with the mode opened at 1000 Hz and 2000 Hz (custom
   configuration on both the bench's transmitter and receiver).

If (2) already rescues DATAC3 and only the 3-4 carrier modes need (3), the
feature can be limited to those. If on-air telemetry (spec 10) shows
narrowband interference is rarely the limit, stop here.

## Design (if phase 0 justifies it)

1. **Slots.** A fixed table per narrow mode, known to both ends:
   five centres for 187-250 Hz modes (700, 1100, 1500, 1900, 2300 Hz), three
   for DATAC3 (900, 1500, 2100 Hz). Slot 0 is always 1500 Hz.
2. **Instances.** One decoder instance per (mode, slot) is too many to keep
   open; open the bound slot's instance on demand in `rx_decoder_bind_mode`
   and cache the last two. Transmit instances likewise.
3. **Choice.** The receiver ranks slots by the noise-plus-interference power
   in each slot's band from the gap spectrum (spec 10), with hysteresis
   (change only for a 6 dB advantage, at most once per 3 rounds).
4. **Signalling.** The receiver names the slot for the round in its POLL.
   No free bits (handbook): candidates are the escape rung value with the
   slot in a reused field, or a slot implied by `poll id mod nslots` chosen
   by the receiver. Design together with the other wire proposals so the
   escape value is not claimed twice.
5. **Hopping variant (no feedback).** Slot for frame `j` of round `R` =
   `PRF(session seed, R, j) mod nslots`. Both ends compute it. This turns a
   steady interferer or a slow notch into the loss of a fraction of frames,
   which the erasure code absorbs. Needs fast re-tuning of the bound
   instance between bursts (within the 100 ms gap), which is the hard part;
   prefer the receiver-chosen variant first.
6. **Control frames** stay at 1500 Hz.

## Risks

- Radio passbands are not flat: level and group delay at 700 or 2300 Hz
  differ from 1500 Hz and differ per radio. Each slot must be checked on
  real radios; expect to drop the outer slots.
- Acquisition false-alarm and frequency-offset behaviour were tuned at one
  centre; re-run `acq_tuning_sweep` and `offset_acq_sweep` per slot.
- Extra instances cost memory and open time on a Pi.

## Steps

1. Phase 0 and a findings note; decision with the maintainers.
2. Custom-centre open path and unit tests (round trip at each slot).
3. Capability, signalling, slot choice; spec and TLA+.
4. Sim: an interferer model that kills frames in one slot only.
5. Integration test with a tone in the audio bridge; on-air trial.

## Acceptance

- Off or capability absent: identical to trunk.
- With a carrier at +10 dB on 1500 Hz, DATAC15 sessions complete where stock
  sessions fall to the floor or fail.
- No loss without interference (slot stays 0).

## As built / Phase 0 (2026-11-14)

Bench only; findings and tables in `docs/SUB-BAND-PHASE0-FINDINGS.md`. No
wire change, slot table, signalling or carousel change was made.

- `utils/rx_bench`: trailing arguments `fsk_hz fsk_db` (2-FSK interferer,
  45.45 baud, 170 Hz shift, random data, its own generator, pre-rolled for
  the notch), `RX_BENCH_CENTRE=Hz` (transmitter and receivers opened through
  `FREEDV_MODE_DATA_CUSTOM` with the mode's own `ofdm_init_mode()` config and
  both centres changed), and the probe token `bp` (a bench-only band-pass).
  `utils/subband_phase0.sh` runs the grid of a mode. The custom open needed
  nothing inside `modem/freedv/`: step 2 of the list below is, for the bench,
  already done; for the product the instance cache and the unit tests of
  the slots are still to do.
- Result: a steady carrier is covered by the notch (60 of 60 at +20 dB for
  DATAC15/16/4/3). A modulated narrow interferer on the mode is not covered
  by `nw`, `nt` or both (0 of 60 from +10 dB for every mode) and is recovered
  completely by a mode at 1000 or 2000 Hz for DATAC15, DATAC16 and DATAC4.
  DATAC3 moved still loses acquisition to an interferer 10 dB over it (it
  has no receive filter); a band-pass fixes that.
- Separation needed (DATAC15, FSK): 300 Hz at +10 dB, 350 Hz at +20 dB, so
  the 400 Hz slots of the design are about right for the narrow modes.
- Open: how often such interference is what limits a real link (proposal 10
  and on-air), the radio's passband at the outer slots, fading, other
  modulations. The decision with the maintainers (step 1) is still to be
  taken; a receive filter for DATAC3 and DATAC1 is a possible receiver-only
  step on its own.
