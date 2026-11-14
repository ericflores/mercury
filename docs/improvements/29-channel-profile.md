# 29 — A cyclic prefix chosen from the measured channel (issue #29)

**Kind:** wire change (new mode variants, negotiated). **Depends on:** the
capability scheme; 26 or 10 (a delay measurement to choose from); makes
most sense with or after 15 (both are about speed with SNR to spare).
**Switch:** `MERCURY_PREFIX_PROFILE=1` / `[arq] prefix_profile`, default
off; used only when both ends announced the capability.

## Goal

On a channel whose paths arrive within 2 ms of each other, send the QPSK
rungs with a 2 ms cyclic prefix instead of 6 ms: the same frames in 18 %
less airtime.

## Facts

- Geometry (`ofdm_mode.c`): every data mode has `ts` 16 ms; `tcp` is 6 ms
  (DATAC1, 3, 4, 15, 16, 17) or 4 ms (QAM16C2); one pilot row in five
  (`ns` 5). 700D, in the same table, uses 2 ms: the prefix is an ordinary
  parameter of the modem.
- Measured (`TRANSMIT-SIDE-RESEARCH.md` section 3, `rx_bench` with
  `RX_BENCH_TCP_MS`): DATAC1 with 2 ms, burst 3.60 s instead of 4.40 s.
  Stock receiver: the same delivery on white noise, CCIR Good, Moderate
  and Poor (2 ms). With 09 + 19: the white-noise cliff moves from −1.25 to
  −0.6 dB (proposal 19 was recycling the unused prefix); under fading the
  differences are inside the noise of 60 trials except at the lowest
  Moderate point (32 against 40 of 60).
- QAM16C2 with 2 ms instead of 4 ms: the same on white noise and CCIR
  Good; on CCIR Moderate fewer bursts are acquired (27 to 34 of 40 against
  34 to 37). Excluded.
- A longer prefix does not rescue the 7 ms NVIS preset (stock 28, 24, 24,
  12 of 60 with 10 ms against 10, 9, 6, 0; proposal 26 gets 56, 54, 51, 32
  with the standard prefix). Excluded.
- Sparser pilots (`ns` 9): the same on white noise and at 0.5 Hz, worse at
  1 Hz, and only 5 % of airtime because the preamble and postamble are one
  modem frame each. Excluded.
- The receiver has two delay measurements: the matching-pursuit profile of
  proposal 26 (per burst, modes of nine carriers or more) and `dly=` in
  proposal 10's telemetry line (`modem/modem_freedv.c`; note
  `docs/CHANSTATS-FINDINGS.md`: `dly` reads 0 on the 7 ms preset, so it
  cannot be the only input).
- `rx_bench` opens a variant by taking the mode's configuration from
  `ofdm_init_mode()`, changing `tcp`, and calling `freedv_open_advanced`
  with `FREEDV_MODE_DATA_CUSTOM`.
- Mode timings are a table (`arq_mode_table`, `arq_protocol.c:61`) and the
  decoders are a pool (`pool_modes[]`, `modem/modem.c:723`).
- The cyclic prefix also feeds the timing estimator; `ftwindowwidth` and
  `timing_mx_thresh` are per mode and were tuned with the standard prefix
  (`docs/ACQ-TUNING-FINDINGS.md`).

## Design

1. **Which rungs.** DATAC1, DATAC17 and DATAC3 (the QPSK rungs that carry
   the traffic). Not the control frames (any station must be able to call
   and poll without knowing a profile), not QAM16C2, not the 3 and 4
   carrier rungs until measured.
2. **Bench first.** The table of section 3.1 for DATAC17 and DATAC3; the
   presets between 2 and 6 ms of delay (`itur-hm` 3 ms, `itur-ld` 6 ms) to
   find where the short prefix starts to lose; a frequency offset and a
   sample-clock offset; acquisition counts (the estimator's thresholds may
   need a value of their own for the variant, decided by
   `utils/acq_tuning_sweep`, not by eye).
   **Stop here if** the short prefix loses more than 10 % of the frames on
   any preset of 2 ms or less.
3. **Choice.** The receiver decides, per session and with hysteresis:
   short when the delay profile it has measured over the last N bursts
   fits inside 1.5 ms (proposal 26's profile: the span of the tracked
   paths) and the link has run on a QPSK rung without loss for a round;
   back to standard on the first round with losses or a wider profile.
   Start conservative: the cost of a wrong "short" is lost frames, the
   cost of a wrong "standard" is 18 % of airtime.
4. **Wire.** Capability `CAP_PREFIX`. One bit says "short prefix on the
   QPSK rungs from the next round". The POLL has no free bit; candidates
   are the escape rung (spec 04, 15 and 28 also want it: one extended
   control frame should serve them all) or a field reinterpreted under the
   capability. To be designed with the capability scheme, not here.
5. **Modem.** The variants are opened like the bench's; they need rows in
   the timing table and a way into the decoder pool (re-bind the payload
   decoder when the profile changes: it changes once in a while, not per
   round).

## Tests

- Unit: with the switch off the modes are byte-identical; a variant
  encodes and decodes noiseless; mixed versions at connect.
- Bench: step 2's tables.
- Integration: sessions over CCIR Good, Moderate and Poor with the
  capability on; the NVIS preset must make the receiver stay on, or return
  to, the standard prefix.
- Simulation: the sim matrix with the choice rule, before any threshold is
  fixed.

## Acceptance

- On CCIR Good and Moderate at 3 dB or more over a QPSK rung's threshold:
  at least 15 % more bytes per second of transmission, and no more frames
  lost than with the standard prefix (within the noise of the run).
- On the NVIS preset and on any preset over 2 ms: no session slower than
  with the switch off.

## Not in scope

A long prefix; pilot spacing; a prefix per burst.
