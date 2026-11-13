# 11 — Two-receiver diversity (issue #11)

**Kind:** receiver only. No wire change. Needs a second receiver and
antenna at the station that enables it.
**Switch:** `[audio] rx_diversity = true` / `MERCURY_DIVERSITY=1`, default off.

## Goal

Decode from two independently faded copies of the signal (left and right
capture channels), combining at the soft-bit level so the two receivers need
not be phase-coherent.

## Facts

- `audioio/audioio.c:114`: `capture_input_channel_layout = LEFT` (also set at
  2955 and 3095). Capture is opened with 2 channels and one is kept. Find
  the option that selects LEFT/RIGHT/STEREO and how samples reach
  `modem.c`'s `rx_thread` (the `capture_i32` buffer, about line 3420).
- `rx_thread` tees one mono stream to the worker rings (`w_ctrl`, `w_pay`,
  listeners), each an `rx_worker_t` with its own decoder instance.
- Soft combining of two LLR vectors of the same codeword exists as the HARQ
  fallback (`freedv_700.c:632-690`): average, parity gate, CRC.
- Each decoder keeps the LLRs of its last failed packet for HARQ
  (`harq_llr`); spec 05 adds a general "failed LLRs" buffer.

## Design

1. **Capture:** when enabled, keep both channels as two mono 8 kHz streams A
   and B (same sound device, so one sample clock).
2. **Decoders:** a second set of workers (control and payload) fed from
   stream B, bound to the same modes as the first set. Reuse `rx_worker_t`;
   the binding calls (`rx_decoder_bind_mode`) are applied to both.
3. **Combining, per packet:** each chain decodes on its own first. Results:
   - one or both pass CRC → deliver once (deduplicate: both chains deliver
     the same frame within a few ms; key on mode + payload bytes + time).
   - both fail → combine: take chain A's failed LLRs and chain B's failed
     LLRs for packets whose end times agree within ±50 ms, average them,
     decode (the shared decode function of spec 09), apply the HARQ parity
     gate and CRC.
   - one syncs, the other does not → nothing more to do.
4. **Where the combine runs:** in a small "combiner" step owned by the
   payload plane: each worker, on a failed packet, posts
   `{mode, end_time, llr copy}` to a two-slot mailbox; whichever worker posts
   second performs the combined decode. Protect with a mutex; LLR vectors are
   up to 16 200 floats.
5. **MFSK:** the same with `h->llr_di` in `modem_mfsk.c` (phase 2).
6. **SNR reporting:** report the better chain's SNR to the carousel; note in
   the log which chain (or the combination) delivered each frame.

LLR scale: average, as the HARQ path does (pitfall 3).

## Steps

1. Bench first, no plumbing: in `rx_bench`, a `--diversity` mode that passes
   one burst through two `chanutil` channels with different seeds, decodes
   each, and adds the "combined" column. Modes DATAC15, DATAC1, DATAC17,
   QAM16C2; AWGN (expect about 3 dB) and `ccir-poor`, `itur-mdv`,
   `ccir-flutter` (expect more at the cliff). This decides whether to build
   the rest.
2. Capture of both channels; second worker set; dedup.
3. Combiner; switch; log lines.
4. `tests/integration`: the audio bridge with two differently seeded
   channels into L and R.
5. On-air trial at a station with two receivers; findings document.

## Tests

- `test_combine_two_failed`: two failed LLR vectors of one codeword at the
  cliff decode when averaged (reuses the HARQ unit test's machinery,
  `tests/modem/test_freedv_harq.c`).
- `test_dedup`: both chains delivering the same frame yields one delivery.
- `test_mismatched_packets_not_combined`: LLRs of different packets
  (end times 300 ms apart) are never combined.

## Acceptance

- Off: identical behaviour, no second workers, no CPU change.
- Bench: combined ≥ max(A, B) on every cell; AWGN gain within 0.5 dB of
  3 dB.
- CPU on a Pi 4 with diversity on: report; control plane must not starve
  (the existing ring-full warnings are the alarm).

## Notes and limits

- Noise common to both antennas (a local source) is correlated; the average
  gains less there. Cancelling it needs phase-coherent receivers and
  pre-detection processing: a different feature, out of scope.
- Hardware that fits: two receivers into the two channels of one stereo
  sound device; radios with dual receivers; two-ADC SDRs. Document a tested
  example in `docs/` once one exists.
- Acquisition also benefits (two chances per burst) without any extra code.
