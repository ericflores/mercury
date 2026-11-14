# Improvement programme: handbook

This directory is the working specification for the proposals tracked in
[issue #1](https://github.com/ericflores/mercury/issues/1). It is written so
that a developer (or a coding agent) who was not part of the investigation can
implement any one proposal without rediscovering the code.

Read this file first, then the spec of the proposal you are working on.

## Status and order

Work top to bottom unless told otherwise. "Depends on" lists what must exist
first.

| order | spec | issue | kind | depends on | status |
|---|---|---|---|---|---|
| 0 | [00-bench.md](00-bench.md) | #17 | test tooling | nothing | first stage (`utils/rx_bench`), replay of recordings (`utils/rx_replay`), tone and impulse interference in the bench and on a session's channel, the false-accept controls, an FSK interferer and the transmit-side probes in the bench (`utils/ldpc_ir_sweep`, prefix and pilot variants, a band-stop, a transmitter hole); inverter presets, MFSK path, a keyed interferer on a session's channel to do |
| 1 | [09-channel-estimation.md](09-channel-estimation.md) | #9 | receiver only | 00 | implemented behind `MERCURY_CHANEST`, bench findings in `docs/CHANNEL-ESTIMATION-FINDINGS.md`, sessions in `docs/INTEGRATION-AB-FINDINGS.md`; offset runs, Pi timing to do |
| 2 | [19-cp-recycling.md](19-cp-recycling.md) | #19 | receiver only | 09 | implemented behind `MERCURY_CP_RECYCLE`, bench findings in `docs/CP-RECYCLING-FINDINGS.md`, sessions in `docs/INTEGRATION-AB-FINDINGS.md`; guard/margin sweep, offset runs, Pi timing to do |
| 3 | [26-window-placement.md](26-window-placement.md) | #26 | receiver only | 09, 19 (the attempt list), 23 | implemented behind `MERCURY_PROFILE_TRACK`, bench findings in `docs/PROFILE-TRACKING-FINDINGS.md`: on disturbed NVIS DATAC1 12 → 55 of 60 at +7.8 dB, DATAC3 27 → 53; sessions in `docs/INTEGRATION-AB-FINDINGS.md`; a second opinion at the unique word (the gap to the prototype), other presets, offsets, Pi timing to do |
| 4 | [23-rx-effort.md](23-rx-effort.md) | #23 | receiver only | nothing; gates 09 as a default and every later fallback attempt | step 2 (early termination) implemented behind `MERCURY_RX_EARLY_STOP`, findings in `docs/RX-EFFORT-FINDINGS.md`; cost measured on x86 only; budget, effort levels, helper thread, Pi timing to do |
| 5 | [07-noise-aware-llr.md](07-noise-aware-llr.md) | #7 | receiver only | 00, 09 (shares the fallback path) | implemented: weights behind `MERCURY_NOISE_WEIGHT` and the automatic carrier notch behind `MERCURY_NOTCH`; findings in `docs/NOISE-WEIGHT-FINDINGS.md`: the notch for unmodulated carriers, the weights for a modulated narrow signal inside a wide mode (DATAC1 0 → 39 of 40 with an RTTY-like signal 3 dB under the burst); time weights, fading with an FSK interferer, on-air to do |
| 6 | [08-impulse-blanker.md](08-impulse-blanker.md) | #8 | receiver only | 00 | implemented behind `MERCURY_BLANKER`, findings in `docs/BLANKER-FINDINGS.md`; MFSK bench, periodic mode, real static to do |
| 7 | [02-qam16c2-tx-clip.md](02-qam16c2-tx-clip.md) | #2 | transmitter only | nothing | implemented behind `MERCURY_QAM16C2_CLIP`: PAPR 11.62 → 6.69 dB at the same peak (`docs/CLIPPED-QAM16C2-FINDINGS.md`, "As shipped"); on-air and the ladder threshold to do |
| 8 | [20-decision-directed.md](20-decision-directed.md) | #20 | receiver only | 09, 19, 23 | first phase implemented behind `MERCURY_DD`, findings in `docs/DECISION-DIRECTED-FINDINGS.md`; the genie as a bench variant, the NVIS phase to do |
| 9 | [03-directed-acquisition.md](03-directed-acquisition.md) | #3 | receiver only | 00 | not measured |
| 10 | [21-full-frame-acquisition.md](21-full-frame-acquisition.md) | #21 | receiver only | 00; complements 03 | arithmetic only |
| 11 | [10-channel-telemetry.md](10-channel-telemetry.md) | #10 | receiver only, logging | 07 (residuals) | implemented behind `MERCURY_CHANSTATS` (a line per packet: mode, ok, path, SNR, offset, parity, iterations, noisiest carrier, Doppler, delay, EVM, first classification), findings in `docs/CHANSTATS-FINDINGS.md`: AWGN, tone, flutter and a clipped transmitter named in 97-100 % of frames, the 7 ms disturbed NVIS case is not detected; gap statistics, blanker fraction, round line, status snapshot, `onair_logs.py` to do |
| 12 | [06-mfsk-metrics.md](06-mfsk-metrics.md) | #6 | receiver only | 00 | not measured |
| 13 | [05-apriori-redecode.md](05-apriori-redecode.md) | #5 | receiver only | 09 (fallback path) | not measured |
| 14 | [12-osd.md](12-osd.md) | #12 | receiver only | nothing | not measured |
| 15 | [11-diversity.md](11-diversity.md) | #11 | receiver only, needs hardware | nothing | not measured |
| 16 | [22-neural-receiver.md](22-neural-receiver.md) | #22 | research (GPU for training) | 00, 20 as the baseline to beat | not started; yardsticks measured |
| 17 | [27-impairment-classifier.md](27-impairment-classifier.md) | #27 | research (GPU for training); receiver only if it wins | 10 (features and the rules to beat), 00, 23; 07 and 08 for most of its actions | not started; design only |
| 18 | [04-carousel-chase.md](04-carousel-chase.md) | #4 | wire change | capability negotiation (below) | modem part measured upstream; proposal 28 builds on its stores and request |
| 19 | [14-short-control.md](14-short-control.md) | #14 | wire change | capability negotiation | arithmetic only |
| 20 | [13-multi-packet-bursts.md](13-multi-packet-bursts.md) | #13 | wire change | capability negotiation, 03 | arithmetic only |
| 21 | [15-higher-rates.md](15-higher-rates.md) | #15 | wire change | 09, 02 | arithmetic only; new fact for step 1: `H_16200_9720` punctures to rates 0.79 and 0.88 within 0.4 dB of ideal at code level, so the higher rates may need no new matrices (`TRANSMIT-SIDE-RESEARCH.md` section 2.2; to be confirmed with the 16QAM demapper) |
| 22 | [16-sub-band.md](16-sub-band.md) | #16 | wire change, research | 10, 07 | phase 0 measured on the bench (`docs/SUB-BAND-PHASE0-FINDINGS.md`): the notch covers a steady carrier; a modulated (FSK) interferer on the mode is recovered by nothing receiver-only and by a 500 Hz move for DATAC15/16/4 (DATAC3 also needs a receive filter); how often it happens on the air is not known (needs 10); no wire work started |
| 23 | [24-qsy.md](24-qsy.md) | #24 | wire change, radio control | capability negotiation, busy detector, 10 | design only |
| 24 | [25-vhf-uhf.md](25-vhf-uhf.md) | #25 | new modes and link timings | 00 (FM and wideband channel models), 15, 14, 23 | design only; **on hold** until the owner has radios fit to test with (do not start it) |
| 25 | [30-spectrum-sharing.md](30-spectrum-sharing.md) | #30 | phase 1 receiver only; phase 2 wire change | 07, 10; capability negotiation for phase 2 | measured on the bench with the foreign signal's place known (`TRANSMIT-SIDE-RESEARCH.md` section 4): with its stretch taken out DATAC1 delivers 36 to 40 of 40 under an RTTY-like signal 20 dB over the burst, at 2.7 dB of cost to the link (1.6 dB when the transmitter leaves the hole too); the detector is not built |
| 26 | [28-redundancy-instalments.md](28-redundancy-instalments.md) | #28 | wire change | capability negotiation, 04 (shares its stores and request), 23 (decoder stop test); 03 or 21 for the narrow rungs under their cliffs | measured at code level (`utils/ldpc_ir_sweep`, `TRANSMIT-SIDE-RESEARCH.md` section 2): the existing codes sent in parts are within 0.35 to 1.1 dB of ideal at every level; 30 % less airtime over the cliff, a smooth rate under it; nothing through the modem yet |
| 27 | [29-channel-profile.md](29-channel-profile.md) | #29 | wire change | capability negotiation, 26 or 10 | measured on the bench (`TRANSMIT-SIDE-RESEARCH.md` section 3): DATAC1 with a 2 ms prefix, bursts 18 % shorter at the same delivery with the stock receiver up to 2 ms of delay, 0.6 dB of cost with 19 on; a long prefix does not rescue NVIS and sparser pilots are not worth it (both measured, not proposed) |

Update the status column and tick the task list in the issue as work lands.

[COMPUTE-AND-NEURAL-RESEARCH.md](COMPUTE-AND-NEURAL-RESEARCH.md) is the research note behind
proposals 19 to 23: how far each mode is from an ideal receiver, what a
decode costs, what was tried and did **not** work (more decoder iterations,
ensembles of decodes, salvaging pieces of failed frames), and where neural
networks do and do not apply. Read it before proposing anything that spends
more compute.

[MODEM73-NOTES.md](MODEM73-NOTES.md) goes through another open modem
technique by technique and says which proposal each maps to; one of its
ideas led to proposal 26.

[TRANSMIT-SIDE-RESEARCH.md](TRANSMIT-SIDE-RESEARCH.md) is the research note
behind proposals 28 to 30: how far each rung is from the Shannon limit and
where the dB go, what the existing codes do when sent in parts, what
another cyclic prefix or pilot spacing buys, what a narrow foreign signal
does to a wide mode and what a hole in the passband costs, each mode's
peak-to-average ratio, and what was tried and did **not** pay (a long
prefix for NVIS, sparser pilots, harder clipping of the QPSK rungs). Read
it before proposing anything on the transmit side.

## The four rules

1. **One switch per feature, off by default.** With the switch off, behaviour
   and (for the modem) output samples and decoded bytes are identical to
   trunk. Prove it with a test.
2. **Receiver-only before wire changes.** A receiver-only feature must work
   against an unmodified peer.
3. **Wire changes are negotiated.** A peer that does not announce the
   capability gets today's frames, bit for bit.
4. **Measured, not asserted.** Every feature lands with its bench table and a
   findings document in `docs/` (see `docs/CLIPPED-QAM16C2-FINDINGS.md` and
   `docs/ACQ-TUNING-FINDINGS.md` for the house style: question, answer,
   method, tables, caveats, how to reproduce).

The project's own history shows why rule 4 matters: a plausible ladder change
was implemented and then rejected because the bench showed −23 % throughput
(`docs/FADE-CLIFF-DECISION.md`, "Update 2026-07-05"). Do not change a tuned
constant in `datalink_arq/carousel.c` without the sim matrix that set it.

## Repository map for this work

| path | what it is |
|---|---|
| `modem/freedv/` | vendored codec2 (OFDM modem, LDPC), with Mercury changes. C, built into `libfreedvdata.a`. |
| `modem/freedv/ofdm.c` | OFDM modulator, acquisition, demodulator, sync state machines. `ofdm_demod_core()` at about line 1783. |
| `modem/freedv/ofdm_mode.c` | per-mode parameters (carriers, pilots, cyclic prefix, thresholds, clipper, LDPC code name). |
| `modem/freedv/freedv_700.c` | per-packet receive path: symbol buffer, de-interleave, LLRs, LDPC, CRC, HARQ fallback (`freedv_comp_short_rx_ofdm`, about line 518). |
| `modem/freedv/mpdecode_core.c` | soft demapper (`Demod2D`, `Somap`, `symbols_to_llrs`) and the sum-product LDPC decoder. |
| `modem/freedv/freedv_api.{c,h}` | public API. Bench-facing setters live here (`freedv_set_timing_mx_thresh`, `freedv_set_tx_clip_config`). |
| `modem/mfsk/` | the non-coherent 32-tone MFSK floor modem. Soft demod in `mfsk.c:392-470`. |
| `modem/modem.c` | glue: mode pool, TX audio building, RX thread, per-plane decoder workers. |
| `modem/modem_backend.h`, `modem_freedv.c`, `modem_mfsk.c` | the backend vtable and its two implementations. New per-instance controls are added here as optional, null-checked hooks. |
| `datalink_arq/carousel.{c,h}` | the data plane: blocks, pieces, rounds, polls, link adaptation. |
| `datalink_arq/arq_fsm.c`, `arq_protocol.{c,h}` | connect/teardown state machine, CALL/ACCEPT frames, mode timing table, SNR thresholds. |
| `common/watterson.{c,h}` | the HF channel model and its named presets. |
| `common/cfg_utils.{c,h}`, `common/mercury_engine.c` | ini keys and where they are applied at start-up. |
| `utils/` | bench instruments. `bench_common.{c,h}` and `chanutil.{c,h}` are the shared parts. |
| `tests/` | Unity unit tests; `tests/sim/` is the deterministic two-station protocol simulator. |
| `tests/integration/` | Go tests that run two real `mercury` processes over simulated audio. |
| `specs/` | TLA+ models of the carousel, connect, turns, teardown. Protocol changes update these. |
| `docs/` | findings and design records. `MODES.md`, `CAROUSEL-ARQ.md`, `CAROUSEL-SPEC.md`, `CHANNEL-MODEL.md` are required reading. |

## Build and test commands (verified on Linux)

```sh
make -C modem/freedv libfreedvdata.a -j8     # the modem library, a few seconds
make -C utils                                 # all bench tools
make -C tests test                            # every unit test (what CI's "UT" job runs via `make test`)
make -C tests test_carousel && ./tests/test_carousel       # one test
make -C tests carousel_bench && SIM_STATS=1 ./tests/carousel_bench 1 clean   # protocol sim, one run
make                                          # the mercury binary (needs the audio/UI dependencies)
make integration-test                         # Go integration tests (needs `make` first)
make -C tests SANITIZE_ASAN_UBSAN=1 test      # what the Sanitizers job runs
```

CI workflows are in `.github/workflows/`: `ut.yml` (amd64 and i386),
`integration.yml`, `sanitizers.yml` (TSan, ASan+UBSan), `build.yml`. New code
must be clean under `-Wall -Wextra` in `utils/` and under both sanitizers.

## How to add a switch

Environment variable (quick A/B, read once):

```c
static int feature_enabled(void) {
    const char *e = getenv("MERCURY_FEATURE");
    return e && e[0] == '1';          /* default OFF: unset means off */
}
```

Existing examples: `harq_enabled()` in `modem/modem.c:2008` (default on),
`MERCURY_NAV` and `MERCURY_SHORT_RUNG` in `datalink_arq/arq_fsm.c:827-836`.
New features default **off**, so test for `'1'`, not for "not `'0'`".

Ini key (persistent), following `tx_gain_db` / `max_keydown_s`:

1. `common/cfg_utils.h`: a `CFG_KEY_*` macro (`"section:key"`) and a field in
   the config struct.
2. `common/cfg_utils.c`: default (about line 68-75), read with
   `iniparser_get*` (about line 435-463), write (about line 594-606).
3. `common/mercury_engine.c:194-195`: apply it at start-up by calling a setter
   (`arq_set_*` or `modem_set_*`).
4. `mercury.ini.example`: document the key in its section.
5. The environment variable, when set, overrides the ini value.

Per-decoder-instance controls go through the backend vtable
(`modem/modem_backend.h`): add an optional function pointer, implement it in
`modem/modem_freedv.c` (and `modem_mfsk.c` if it applies), and null-check at
the call site, exactly as `set_harq` / `harq_reset` are done. Apply it where
instances are opened or bound: `pool_open_mode_locked()` (`modem.c:656`) and
`rx_decoder_bind_mode()` (`modem.c:2050`).

Changes inside `modem/freedv/` should stay small and guarded by a field that
defaults to the stock behaviour, with a `freedv_set_*` / `freedv_get_*` pair
in `freedv_api.{c,h}`.

## Switches implemented so far

All off by default. The environment variable overrides the ini key, which
is in `[audio]`. "Token" is the name in `utils/rx_bench` and
`utils/rx_replay` variants.

| proposal | environment | ini key | what it does | token |
|---|---|---|---|---|
| 09 | `MERCURY_CHANEST=auto` | `rx_chanest = auto` | decode attempt from a channel estimate over four pilot rows (decode path 2) | `t4`, `t4f3`, `t4f5` (`auto` in `rx_replay`) |
| 19 | `MERCURY_CP_RECYCLE=4` | `rx_cp_recycle = 4` | attempt from rows with the cyclic prefix averaged in (path 3) | `cpN` |
| 26 | `MERCURY_PROFILE_TRACK=1` | `rx_profile_track = true` | the burst demodulated again with the FFT window following the delay profile (path 4) | `pt` |
| 07 | `MERCURY_NOISE_WEIGHT=1` | `rx_noise_weight = true` | attempt with interfered carriers' soft bits scaled down (path 5) | `nw` |
| 20 | `MERCURY_DD=1` | `rx_decision_directed = true` | attempt from a channel estimate that also uses the decoder's belief about the data (path 6) | `dd` |
| 07 | `MERCURY_NOTCH=1` | `rx_notch = true` | automatic notch for steady carriers on the captured audio, every decoder | `nt` |
| 08 | `MERCURY_BLANKER=8` | `rx_blanker = 8` | impulse blanker on the captured audio, every decoder | `bK` |
| 23 | `MERCURY_RX_EARLY_STOP=1` | `rx_early_stop = true` | abandon LDPC decodes that are going nowhere (cheaper, not better) | `es` |
| 02 | `MERCURY_QAM16C2_CLIP=1` | `qam16c2_tx_clip = true` | clip QAM16C2 on transmit: same peak, 4.9 dB more average power | (see `utils/qam16_papr_sweep`) |

For a trial, `MERCURY_RX_ALL=1` (or `[audio] rx_improvements = true`) turns
on every receiver-only switch of this table at the setting shown. A
feature's own environment variable still decides for that feature, on or
off. The transmit-side switch is not part of it.

The order of the decode attempts on a packet whose stock decode failed:
HARQ combine (where enabled), noise-weighted (only when the profile singles
out a carrier), alternative estimate, recycled prefix, profile replay,
decision-directed. Each stops the list when its CRC passes. Every attempt
after the stock one is behind the same parity gate.

To see them act in a session, the timing log (`-L file`) has one line per
frame an optional attempt delivered (`rx decode path N`), a line when a
notch engages or is released (`carrier notch: 1650.0 Hz`), and one every
10 s in which the blanker removed anything.

## Measurement method

- **SNR3k** is signal power over noise power in 3 kHz. On AWGN it is set
  directly. On a fading preset the knob is the noise density `No` and the
  achieved SNR3k is measured and reported per row; `SNR3k ≈ −No + A` with `A`
  depending on the waveform's average power at the normalised peak (for
  DATAC1 at the bench's peak of 4000, `A ≈ −30.1`; for QAM16C2 `A ≈ −34.4`).
- **Equal peak.** Bursts are normalised to one peak (`HEADROOM_PEAK` 4000 in
  the tools), leaving int16 headroom for noise. Comparisons between waveforms
  of different PAPR must say whether they are at equal peak or equal average
  power (`docs/CLIPPED-QAM16C2-FINDINGS.md` explains why it matters).
- **Paired trials.** Every variant must decode the *same* samples. Build the
  whole impaired buffer first, then feed it to each variant's decoder. Follow
  the RNG discipline in `utils/bench_common.h` (fixed draw counts, pre-drawn
  tail).
- **Acquired / delivered.** Acquired = the decoder reached full
  `FREEDV_RX_SYNC` (trial sync does not count). Delivered = a frame came out
  with CRC ok **and** its bytes equal what was sent.
- **Stock row first.** The stock variant is always a column, and it must
  reproduce the documented benches (`docs/MODES.md`,
  `docs/ACQ-TUNING-FINDINGS.md`) before any other column is believed.
- **Trials.** 100 per point minimum for a findings document; 200 where the
  delivery is below 20 % or above 80 %.
- **Channels.** At least AWGN, `ccir-poor`, `itur-mdv` (disturbed NVIS, 7 ms)
  and `ccir-flutter`. List with `utils/watterson_test --list-presets`.
- **Neutrality control.** A receiver feature must show no loss where there is
  nothing for it to fix (clean channel, high SNR, every rung).
- **False accepts.** Any feature that adds decode attempts must run a
  noise-only and a wrong-session control and report how many frames passed
  CRC that should not have.
- **Protocol effect.** Modem-level gains are then checked end to end:
  `tests/sim` (60-100 seeds per channel, as in the comments of `carousel.c`)
  and `tests/integration` (`MERCURY_CH_ENGINE=watterson MERCURY_CH_FADING=...
  MERCURY_CH_NO=... go test -C tests/integration -run 'TestMercuryARQTransfer$'`;
  two builds with `MERCURY_TEST_BIN_A` / `MERCURY_TEST_BIN_B`).
- **Sessions, and what the stations heard.** A session A/B is the same test
  run twice, switches off and on, over several seeds, on the lockstep
  transport (`MERCURY_TEST_TRANSPORT=sock`, paced, never
  `MERCURY_TEST_SOCK_FAST`): compare the time to deliver, and count the
  `rx decode path` lines to see that the switches acted. Two sessions are
  not paired (pitfall 19), so the exact comparison is made on recordings:
  `MERCURY_TEST_RX_DUMP=1` with `MERCURY_TEST_LOGDIR` records what each
  station's decoders were handed, and `utils/rx_replay FILE variants modes
  seed` decodes that same audio with every variant and reports frames
  delivered, gained and lost against the first. "lost" must be 0 for
  anything that only adds attempts. Interference goes on a session's channel
  with `MERCURY_CH_TONE=Hz,dBFS` and `MERCURY_CH_IMPULSES=N,dBFS[,ms]`.
  `docs/INTEGRATION-AB-FINDINGS.md` is the worked example.

## Test equipment on hand

The fork owner's station, as stated by the owner on 2026-11-13. None of it
has been used with Mercury in this programme yet; everything measured so
far is bench only.

| equipment | what it is | what it allows here | limits |
|---|---|---|---|
| Icom IC-7300 | HF and 50 MHz transceiver with a USB audio codec and CAT on one cable (Hamlib model 3073) | the station for every on-air test of the HF proposals. For 24: Hamlib can tune it, and its band-scope over CAT is the one case Hamlib's spectrum code was tested on (Hamlib's own notes), so a coarse occupancy map needs no extra hardware. For 02: clipping and ALC measured on a real transmitter | one transceiver: a two-way session needs a second Mercury station somewhere |
| SDRplay RSP1B | receive-only SDR, 1 kHz to 2 GHz, up to 10 MHz of spectrum at once | a second, independent receiver. Recordings of local noise (inverters, power lines) and of the IC-7300's own signal for 00-bench, 07, 08 and 22; the second receiver 11 needs; a wideband view of the neighbouring channels for 24 | cannot transmit. Mercury has no backend for it: feed Mercury from an SDR program's audio output, or record and replay |
| Alinco DJ-MD5FXT | VHF/UHF handheld (DMR and analog FM) | nothing for now | the owner judges it a poor platform to test with; proposal 25 is on hold for that reason |

What one operator can test alone with this:

- **A radio-frequency loop on the bench**: the IC-7300 into a dummy load,
  the RSP1B as the receiver. Real transmitter, real receiver, no
  ionosphere. Good for transmit-side questions (02) and for checking that
  a receiver feature survives real hardware.
- **Real noise with synthetic bursts**: record the local noise with either
  receiver and add modem bursts to it on the bench (the data recipe of
  proposal 22, and what 00-bench's replay tool is for). Exact labels, real
  interference, no second station.
- **One way over a real path, to be verified before relying on it**:
  transmit with the IC-7300 and record the signal at a remote public web
  receiver, then replay the recording through the receiver variants. It
  needs 00-bench's replay tool, a receiver whose terms allow it, and the
  usual identification.
- **Two-way sessions** (anything the carousel does, 24's negotiation) need
  a second station running Mercury, or the simulators.

## Wire format constraints (read before any protocol change)

**The POLL has no free bit.** `encode_ctl()` in `datalink_arq/carousel.c:453`
uses all 14 bytes:

| byte | bits |
|---|---|
| 0 | type (2: 0 POLL, 1 REQ, 2 STATUS, 3 POLL-ending), has_data (1), poll id (4), ctl_deaf (1) |
| 1 | rung (3), frames asked (4), SNR bit 0 |
| 2 | loss (4), window base (4) |
| 3-9 | 8 × 7 bits: pieces each open block still needs (127 = unseen) |
| 10 | first missing data piece of the base block (255 = none) |
| 11 | SNR bits 5-3, handover rung (3), SNR bits 2-1 |
| 12 | handover frames (4), handover round id (4) |
| 13 | highest block (4), unopened (1), short_rung (1), backlog (2) |

Ways to carry something new, in order of preference:

1. **Reinterpret existing fields under a negotiated capability.** For example
   a repeat request can reuse bytes 3-9 as a frame bitmap when a marker says
   so.
2. **The escape code.** `decode_ctl()` drops a frame whose rung or handover
   rung is ≥ `CAR_NLEVELS` (7). Rung value 7 is therefore unused and an old
   peer discards it as corrupt. It can mark an extended control frame, but
   note that proposal 15 wants value 7 for a new rung; the two must be
   reconciled before either lands.
3. **A longer control frame** in a new mode. Last resort.

**Capability negotiation.** CALL and ACCEPT carry a 2-bit minor revision
(`ARQ_CONNECT_MINOR`, currently 1; `arq_protocol.h:442-476`,
`arq_protocol_connect_minor()`). An ACCEPT with a reserved bit set is dropped
by current stations, so capabilities cannot simply be put in the ACCEPT's
zero bytes. The intended scheme, to be designed once and shared by all wire
proposals:

- Minor revision 2 means "I understand capability bits".
- The caller announces minor 2 in its CALL. A callee that also has minor ≥ 2
  answers with its capability bits in ACCEPT bytes 10-13 (zero today); to a
  minor-1 caller it answers exactly as today.
- Each side then uses a feature only if both announced it. The caller's own
  bits travel in its first control frame or in spare CALL bytes after the
  arithmetic-coded callsign, whichever the design finds room for.
- `specs/connect/` (TLA+) and `docs/CAROUSEL-SPEC.md` are updated in the same
  change, and `tests/datalink_arq/test_arq_protocol.c` gets the mixed-version
  cases (`test_connect_other_version_refused` is the model).

Design this once, as its own small change, before the first wire proposal.

## Conventions

- **Language:** C (gnu11). No new dependencies.
- **Commits:** `area: what changed, in plain words` (`carousel: ...`,
  `tnc: ...`, `utils: ...`, `docs: ...`). One logical change per commit.
- **Comments:** the house style explains *why* with the measurement that
  justified it. Keep that up; cite the bench and the numbers.
- **Tests:** Unity (`tests/unity`). Add the binary to `TEST_BINS` in
  `tests/Makefile` with a rule like `test_freedv_harq`'s (line 258). Tests
  must be deterministic (seeded) and fast.
- **Logging:** `HLOGI/HLOGW/HLOGD(component, fmt, ...)`; `car_trace()` inside
  the carousel.
- **Branch:** `claude/mercury-adaptive-modem-o5hsi7` on this fork. One draft
  PR per proposal is easiest to review.

## Pitfalls already paid for

These cost time during the prototype; do not repeat them.

1. **Smoothing channel estimates across carriers needs phase-slope removal.**
   The FFT window may start anywhere in a 5 ms span inside the cyclic prefix
   (`ofdm.c`, "keep sample_point inside cyclic prefix"), which rotates
   adjacent carriers by tens of degrees. Averaging neighbours without
   de-rotating delivered 0/20 at +5 dB while looking fine at 0 dB and +15 dB.
   Always test a feature at several SNRs, not only at the cliff.
2. **The first and last modem frame of a burst lack a pilot row.** The
   "previous" pilot of the first frame is the preamble (different amplitude
   scaling) and the "future" pilot of the last frame is past the postamble.
   Detect them with `sync_state == trial && sync_counter == 0` and
   `sync_state == synced && modem_frame == np - 1`.
3. **The LDPC decoder is very sensitive to LLR scale.** At DATAC17's AWGN
   cliff, changing the fixed `EsNodB` from 3 to 0 or 6 took delivery from
   26/60 to 0/60. Anything that changes soft-bit magnitudes (weights,
   combining, a new estimator) must re-check the scale. The HARQ code
   averages instead of summing for this reason (`freedv_700.c:640-652`).
4. **`modem/freedv/ofdm.c:2112` overwrites the amplitude estimate** chosen by
   `amp_est_mode` a few lines above. Know this before reasoning about
   amplitudes.
5. **Modes differ in cyclic prefix:** 6 ms for the DATAC modes, 4 ms for
   QAM16C2 (`ofdm_mode.c`).
6. **Bench output is block-buffered when redirected.** `fflush(stdout)` after
   every row or a long run looks dead.
7. **`strtod("0e6")` is zero times ten to the sixth.** Do not use `e` as a
   field letter in a compact variant syntax.
8. **A fading run is slow at the cliff** (each failed LDPC decode runs 100
   iterations). Budget minutes per row for the 16200-bit modes.
9. **Frame sizes identify modes** in several places (`arq_protocol.c`, the
   note on DATAC13). A new mode needs a payload size no other mode has.
10. **The session streams data to the application before the block CRC-32 is
    checked** (`deliver_in_order`, `carousel.c:1452`). A failed block check
    ends the session on purpose.
11. **Match bench variant names exactly.** A substring test made the variant
    `it400` (400 decoder iterations) also match `t4` and switch on proposal
    09's estimator; the result looked like a 1 dB gain from iterations that
    does not exist. A sweep over several values exposed it (only 400
    "worked"). Always sweep a parameter before believing a single point.
12. **A decode that fails is the expensive case**: all 100 iterations, about
    0.2 s of a server core for the 16200-bit code, against an allowance of
    about 1 s. Every fallback attempt is another one. See proposal 23.
13. **A delivery rate that does not rise with SNR is not a noise problem.**
    DATAC1 on disturbed NVIS delivers 20-30 % at +8 dB and at +18 dB alike.
    A better estimate (09) and a better decoder do nothing there; the FFT
    window and the timing tracker were the limit (proposal 26). Sweep the
    SNR well above the cliff before deciding what a channel is limited by.
14. **Two receivers with different FFT windows are not exactly paired.**
    Each sees a different stretch of every symbol, so on white noise at a
    cliff they can differ by several frames in a hundred with nothing wrong.
    A forced window 4 samples from stock's lost 70 frames in 200 at DATAC1's
    cliff for a different reason again: it followed every wobble of the
    timing estimate, which stock's wide hysteresis ignores.
15. **A measurement made on the pilots is blind at some frequencies.** The
    pilot residual of proposal 07 (this pilot against the mean of its
    neighbours in time) cancels anything whose phase advances by whole turns
    from one pilot row to the next: a carrier at any multiple of 1/frame
    period (9.09 Hz for the 6 ms-prefix modes). A tone at 2300 Hz, 3 dB under
    a DATAC1 signal, left the profile flat: 0 of 60. The data symbols see it.
    When an estimator uses one kind of reference, ask what it cannot see.
16. **A carrier does its damage before the soft bits.** On a carrier's
    centre it captures the frequency tracker, which sums the pilots of all
    carriers (the estimate walks off by 2 Hz and every carrier rotates); a
    few dB above the signal it stops acquisition. Weighting soft bits cannot
    repair either. The notch (`modem/rx_notch.c`) removes it first.
17. **A rule from a specification is a starting point, not a result.** The
    early-termination rule of proposal 23 as specified (stop under 97 % of
    the parity checks) lost 3 DATAC1 frames in 37 at the cliff: a decode that
    ends up converging can sit between 80 and 97 % for more than ten
    iterations. Run the no-loss check before believing a threshold.
18. **`MERCURY_TEST_SOCK_FAST=1` starves the decoders.** The lockstep
    simulator then feeds audio faster than the DSP threads decode it; the
    rings overflow ("decoder ring full") and the session crawls. Use the
    paced default for anything that involves decode time.
19. **Two sessions are not paired, even on one seed.** The harness writes the
    payload about a second after CONNECTED, and the caller's first turn comes
    at the same moment: whether the first keydown carries data or an empty
    poll (9 s more) has been decided by one millisecond. The connect also
    lands on a 20 ms block boundary that varies from run to run, so a
    marginal CALL decodes in one run and not in the next. Compare sessions in
    numbers (several seeds), and use `utils/rx_replay` on the recordings for
    the exact, frame-by-frame comparison.
20. **A harness run can fail before it starts**: the two free ports it picks
    are released before the stations bind them, and now and then one is
    taken ("Could not open TCP port"). Retry; it is not the modem.
21. **The decoder's parity count is not a success test.** `run_ldpc_decoder`
    counts satisfied checks from extrinsic signs: on up to 15 % of frames
    near a cliff it stays one or two short with every bit right (and runs to
    the iteration limit), and with punctured bits it never completes. A
    bench that asks for `pcc == NumberParityBits` reports frames as failed
    that the CRC would deliver. Compare the payload bits.
22. **A result on the narrow modes does not carry to the wide ones.** The
    per-carrier weights were judged redundant after proposal 16's phase 0
    (narrow rungs, where a narrow interferer covers the whole signal, and
    carriers, which the notch removes). Inside a wide mode a modulated
    narrow signal is exactly their case: 0 to 39 of 40. Measure each claim on
    every mode it will be used for before dropping a feature.
23. **`A && (B) & (C) &` in a shell runs only `A && (B)` in the first
    background job**: a variable set by `A` does not exist for `(C)`. Two
    bench outputs went to the wrong directory that way. Set variables on
    their own line.

## Glossary

- **Rung / level:** a payload mode's position in the ladder (0 MFSK, 1
  DATAC15, 2 DATAC4, 3 DATAC3, 4 DATAC1, 5 DATAC17, 6 QAM16C2).
- **Round:** one keydown of up to N data frames, sent in answer to a POLL.
- **Piece / block:** 23-24 byte unit of the erasure code / up to 96 pieces
  that any K of rebuild.
- **LLR:** log-likelihood ratio, the soft value of one coded bit.
- **Pilot row:** the known symbol at the start of each modem frame (every
  `ns` = 5 symbols) used to estimate the channel.
- **Modem frame / packet / burst:** 5 OFDM symbols / `np` modem frames
  carrying one LDPC codeword / preamble + packet(s) + postamble.
- **Control-deaf:** a peer that hears this station below DATAC16's reach; its
  control then goes on the MFSK floor.
- **Cliff:** the SNR range, often under 1 dB wide, where a mode goes from
  delivering nothing to delivering everything.
