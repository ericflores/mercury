# 30 — Sharing the passband with someone else's narrow signal (issue #30)

**Kind:** phase 1 receiver only; phase 2 wire change (negotiated).
**Depends on:** 07 (the per-carrier weights, which are the first line and
stay on with this); 10 (gap statistics) for the detector; the capability
scheme for phase 2.
**Switches:** `MERCURY_BANDSTOP=1` / `[audio] rx_bandstop` (phase 1),
`MERCURY_TX_HOLE=1` / `[arq] tx_hole` (phase 2); both default off.

## Goal

Keep a wide mode working when a narrow signal that is not ours sits inside
its passband, and stop transmitting on top of that signal.

## Facts

- Measured (`TRANSMIT-SIDE-RESEARCH.md` section 4.2; `rx_bench` with its
  FSK interferer, the `bs` token and `RX_BENCH_TX_HOLE`). DATAC1 at +5 dB,
  an RTTY-like signal on 1500 Hz, level relative to the burst's whole
  power, delivered per 40:
  - −3 dB: stock 0, weights 39;
  - +3 dB: stock 0, weights 0, 09 + 19 + weights 36;
  - +10 dB: nothing is acquired; with the occupied 412 Hz taken out of the
    audio, 38 (39 with 09 + 19 + weights);
  - +20 dB: taken out, 6; with 09 + 19 + weights, 36 (40 with the signal on
    2100 Hz; 40 for an FT8-like signal);
  - +30 dB: nothing.
- QAM16C2 at +14 dB: stock 0 of 30 with the signal 10 dB *under* the
  burst; weights 30; taken out, 30 at 0 and +10 dB, 0 at +20 dB.
- Cost of the hole, DATAC1's white-noise cliff: no hole +0.85 dB (−1.25
  with 09 + 19); the receiver takes 412 Hz out, +3.5 (+1.5); the
  transmitter leaves it empty too, at the same average power, +2.4 (+0.4).
- With the transmitter's hole the middle 292 Hz of it hold 48 dB less of
  the burst's power (the depth of the bench's 401-tap filter).
- The automatic notch (`modem/rx_notch.c`) finds lines by the **minimum**
  of each spectral bin over a second, and removes them with 6 Hz notches.
  It does not see a keyed signal (measured: 0 of 40 against the FT8-like
  one). Its structure is the model for the detector here: a spectrum every
  64 ms, a hold over time, engage and release thresholds, "never engages
  on Mercury's own signals" as a unit test.
- Where audio pre-processing sits: `rx_thread()` in `modem/modem.c`, after
  the optional recording: notch (`rx_notch_process`, `:3513`), blanker
  (`rx_blanker_process`, `:3529`), then the decoders. A band-stop belongs
  between the notch and the blanker.
- The receiver knows when its peer is silent: the protocol is
  receiver-driven and half-duplex, and the modem knows when it is keyed
  (the busy detector's mute, `arq_fsm.h`) and when a frame is in sync.
  What is on the channel between the peer's bursts is noise and other
  people's signals.
- Transmit chain: modulator, clipper and transmit band-pass inside the
  OFDM library (`ofdm.c`: `allocate_tx_bpf` at `:620`; clip configuration
  through `freedv_set_tx_clip_config`, `freedv_api.h:394`), then
  `append_burst_audio()` (`modem/modem.c:1404`). A hole cut **before** the
  clipper would be filled again by the clipper's products; it has to be a
  filter on the finished audio, with the peak restored afterwards.
- The MFSK floor hops over 1 kHz; a hole inside it removes tones it needs.
  The narrow OFDM rungs are narrower than a typical hole: for them the
  answer is to move (proposal 16), not to cut.
- Proposal 16's phase 0 measured the narrow rungs under the same
  interferer (`docs/SUB-BAND-PHASE0-FINDINGS.md`).

## Design

### Phase 0: more bench (no code in the modem)

The section 4.2 tables for DATAC17; the same under CCIR Poor; two foreign
signals at once; a foreign signal that starts during the burst. A hole as
wide as SSB voice (2 kHz) leaves nothing: note the widest stretch worth
cutting per mode (expected: a third of the carriers at most, from the
cost table).

### Phase 1: the receiver alone

1. **Occupancy.** `modem/rx_occupancy.{c,h}`, in the pattern of
   `rx_notch`: a 1024-point spectrum every 64 ms, grouped in bins of the
   carrier spacing (62.5 Hz). Keep, per bin, the **lower quartile** of its
   power over the last 20 s (long enough to contain several of the peer's
   gaps: in a gap only noise and other people's signals remain, so the
   quiet moments of a bin are what is there when the peer is not). A
   stretch is occupied when two or more adjacent bins stand `ON` dB over
   the median bin's lower quartile for 5 s, and is released after 30 s
   below `HOLD`. Thresholds from the bench, starting at 10 and 4 dB as the
   notch does.
   The unit tests that decide it: back-to-back bursts of every rung, the
   control mode and the MFSK floor with their real gaps never engage it;
   white noise never engages it; an RTTY-like and an FT8-like signal from
   −3 to +20 dB over a burst are found within a few seconds and to within
   one bin; a signal that stops is released.
2. **Band-stop.** For each occupied stretch (at most two, at most a third
   of the passband in all), a linear-phase FIR band-stop over it plus one
   bin each side, applied to the audio ahead of the blanker. With nothing
   engaged the audio passes bit for bit (the neutrality test). Latency of
   the filter is constant and small against the frame; check the decoders
   and the timing log do not care.
3. **Weights.** They stay on and see the emptied carriers as noisy; no
   coupling is needed. Measure whether telling the receiver explicitly
   which carriers are gone (LLR zero, pilots and unique-word bits there
   ignored) gains over that; build it only if it does.
4. **Telemetry.** A timing-log line when a stretch engages and releases
   (`band-stop: 1290-1710 Hz`), and the stretch in proposal 10's line.

### Phase 2: the agreed hole

1. Capability `CAP_HOLE`.
2. **Report.** The receiver sends the sender up to two stretches, each as
   a first bin and a number of bins (6 + 4 bits on the 62.5 Hz grid).
   The POLL has no free bit: this rides in the extended control frame that
   proposals 04, 15, 28 and 29 also need, or in a STATUS frame under the
   capability.
3. **Sender.** A band-stop on the finished burst audio (after
   `append_burst_audio`, so after the clipper), then the peak brought back
   to where it was. Wide rungs only. The control frames and the floor are
   sent whole.
4. **Receiver.** Applies the same band-stop whether or not the foreign
   signal is still there, so both ends agree on which carriers exist.
5. **Lifetime.** A hole is dropped by the receiver's next report without
   it. A sender that has not heard a report for N rounds sends whole
   frames.
6. Both directions are independent: each end reports what it hears.

## Tests

- Unit: the occupancy tests above; band-stop depth and passband ripple;
  neutrality with the switch off and with nothing engaged.
- Bench: `rx_bench` with the real detector in place of the `bs` probe: the
  section 4.2 table again, with the detector's misses and false alarms
  counted; the false-accept controls.
- Integration: sessions with `MERCURY_CH_TONE` replaced by a keyed signal
  (an option to add to `utils/watterson_test` and the harness, as the tone
  was): stock against phase 1 against phase 2.
- Phase 2: mixed versions at connect; a report lost; a hole that moves.

## Acceptance

- Phase 1: DATAC1 at +5 dB delivers at least 90 % of frames under an
  RTTY-like signal 10 dB over the burst, found by the detector, where the
  stock receiver delivers none; no change in any table of
  `docs/NOISE-WEIGHT-FINDINGS.md` with no foreign signal; the detector
  never engages in the integration suite's clean runs.
- Phase 2: the link's cost of a hole drops by about 1 dB against phase 1,
  and the transmitted power inside the hole is at least 40 dB down.

## Not in scope

Voice-wide interference (move instead: 24); the narrow rungs (16); decoding
and subtracting the foreign signal; interleaving carriers with another
Mercury link (needs a common clock the stations do not have).
