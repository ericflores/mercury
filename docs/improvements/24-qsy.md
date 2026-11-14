# 24 — Move the session to a clear frequency (issue #24)

**Kind:** wire change, negotiated; plus radio control. Not receiver-only.
**Depends on:** the capability negotiation described in the handbook
(connect minor revision 2), which must be designed once before this; the
busy detector; 10 (telemetry) for the mid-session trigger.
**Switch:** `[qsy] enable` / `MERCURY_QSY`, default off. Off: no capability
is announced, no frame changes, and Mercury never sends a tuning command.

## Goal

Call on the published frequency, then carry the traffic on a nearby
frequency that **both** stations find clear, and move again if that one
becomes occupied. Two problems it answers:

- the calling frequency of a busy station (a gateway, a net) is held for the
  whole of every session, so the next caller collides with traffic;
- interference that arrives mid-session today can only be fought with a
  slower rung.

## What exists today

- Nothing in the protocol agrees a frequency. A session stays where the CALL
  was heard.
- Scanning belongs to the host: BPQ32 retunes the radio itself and tells
  Mercury with `LISTEN OFF` / `LISTEN ON` / `BW` (`docs/TNC.md`, "Scanning
  hosts (BPQ32)"). Mercury is not told the frequency.
- `radio_io` can read the dial (`radio_io_get_frequency`, Hamlib
  `rig_get_freq` in `radio_io/radio_io.c`) and cannot set it: there is no
  `rig_set_freq` call in the tree. A Hermes-Lite 2 is the exception: Mercury
  is its SDR host and tunes it (`hl2_set_frequency`, `audioio/hl2.c`, an
  atomic that the stream picks up, so it can change while running).
- Busy detector: `modem/channel_busy.{c,h}`, a tracked noise floor over
  300-2700 Hz in 150 Hz bands, off by default, with the field experience
  behind that default in `mercury.ini.example` (`[channel]`). After a retune
  it needs `BUSY_HOP_HOLDOFF_MS` (1 s) and a new floor
  (`busy_reseed_on_hop`), and a floor taken on an occupied channel hides the
  occupant: a hop must not trust a floor learnt in the first second.
- What a station can hear of its neighbours:
  - a transceiver through its audio port: 2.4-3 kHz, nothing outside the
    channel it is on. A neighbour can only be judged by going there;
  - a Hermes-Lite 2: 48 kHz of I/Q, of which `hl2_dsp` keeps the SSB channel
    (`docs/HERMES-LITE-2.md`). The rest, about ±20 kHz around the dial, is
    received and thrown away today;
  - some transceivers send their band-scope over CAT (Hamlib has a spectrum
    callback, implemented for Icom and tested on an IC-7300 per Hamlib's own
    notes). Coarse, but occupancy does not need more.
- Wire (handbook, "Wire format constraints"; `docs/CAROUSEL-ARQ.md`, "Wire
  formats"): the POLL has no free bit. CALL and ACCEPT are 14 bytes; the
  ACCEPT's bytes 10-13 and four bits of byte 8 are zero today, and a station
  drops an ACCEPT with a reserved bit set, which is why capabilities need
  minor revision 2. The CALL's bytes 4-13 hold the arithmetic-coded
  callsign.
- A frame is bound to its session by the session id and the callee's 16-bit
  nonce, so a stranger's frame on the new frequency cannot join.
- Test rig that already has frequencies:
  `tests/integration/hl2emu_linux_test.go`, two emulated HL2s whose signals
  reach each other only "when both radios are tuned to the same frequency".

## Design

### Vocabulary

- **Home**: the dial frequency the CALL was sent and heard on.
- **Offset**: a signed number of steps from home. Step 500 Hz, one signed
  byte: ±63.5 kHz. An offset is valid only if its magnitude is at least the
  session's occupied bandwidth plus a guard (3000 Hz for BW2300/BW2750,
  1000 Hz for BW500).
- **Allowed set**: `[qsy] allow = lo-hi[,lo-hi...]` in Hz of occupied
  spectrum. Mandatory when the feature is on; empty means the feature is
  off. Mercury never proposes, accepts or tunes to a channel whose occupied
  spectrum is not inside it. This is where the operator puts the licence,
  the band plan, the sub-bands for automatically controlled stations where
  the administration has them, and the frequencies to keep off (beacons,
  nets, other modes' watering holes).
- **Excursion**: `[qsy] max_excursion_hz` (default 15000) from home, so
  repeated moves cannot walk a station across the band or past what its
  antenna tuner and filters hold without retuning.

### Occupancy knowledge

One structure per station, `qsy_map`: for each step inside the allowed set
and the excursion, one of `unknown`, `clear since T`, `busy at T`.

- Transceiver, audio only: every entry is `unknown` except home.
- HL2: a power spectrum of the full 48 kHz stream, averaged per step, with
  the same floor-and-hysteresis logic as `channel_busy` per step. An entry
  is `clear` only after `clear_hold_s` (default 10) without energy.
- CAT band-scope: the same, from the scope lines, if the Hamlib build and
  the radio support it. Later step; the design must not depend on it.

### Phase 1: at connect ("call here, work there")

All of the agreement happens on home, inside the two frames that already
have repeat and loss handling.

1. The caller announces the capability in its CALL (minor 2 scheme).
2. A callee that has it too chooses an offset from its own map: the nearest
   `clear` step, or with no knowledge the nearest allowed step, alternating
   sides. It puts the offset in ACCEPT byte 10 (0 = stay) and, **after its
   ACCEPT keydown**, retunes and listens there.
3. The caller reads the ACCEPT. If the offset is 0, or outside its own
   allowed set or excursion, it stays and repeats the CALL with a "stay"
   mark (a capability bit cleared for this attempt). Otherwise it retunes,
   listens for `listen_ms` (default 2500: the hold-off plus a verdict), and
   - if its own detector says the frequency is busy: returns home and
     repeats the CALL with that offset excluded (one byte in the CALL if the
     callsign leaves room, else "stay");
   - else sends what it would have sent on home: its answer to the ACCEPT
     (the ACCEPT is the first POLL).
4. A callee that hears no frame of the session on the traffic frequency
   within `hop_fail_ms` returns home and waits for the repeated CALL, in the
   state it was in after sending the ACCEPT. Both stations therefore return
   to home by silence alone; no frame has to get through to undo a move.

`hop_fail_ms` is derived, not chosen: radio settle + the caller's listen +
one control frame + the decode allowance (`D_OFDM_MS`, or the floor's when
the ACCEPT said the caller is heard below the control mode) + margin.

### Phase 2: during a session

Needs a control frame the POLL cannot carry: an **extended control frame**
marked by the escape code (rung value 7, which today's stations drop as
corrupt; reconcile with proposal 15, which wanted that value for a rung,
before either lands). Layout to be fixed with the capability design; it
carries a subtype, the session binding, and for this proposal:

- `QSY-REQ(offset, hop id)`, from either station, when its trigger fires;
- `QSY-ACK(offset, hop id)`, or `QSY-NAK`.

After the keydown that carried the ACK, both retune. The station that holds
the turn (the one whose poll is next under the turn rules, `specs/turns`)
listens for `listen_ms` and transmits first. The failure rule is the one of
phase 1: no frame of the session within `hop_fail_ms` → back to the
**previous** frequency, where the session resumes with an ordinary poll.
If the ACK is lost the requester never moved, the other station comes back
after `hop_fail_ms`, and the cost is one lost round.

Triggers, each behind its own threshold and all rate-limited
(`min_dwell_s`, default 60; `max_hops`, default 6 a session):

- energy in the gaps: the passband level in the silences of the turn-taking
  is above the floor in most of the last N gaps (the gap monitor of
  proposal 10);
- rounds lost at an SNR that should carry the rung (loss above the ladder's
  step-down threshold for M polls while the SNR reading stays above the
  rung's `level_min_db`);
- the host or operator: a `QSY` command on the control port.

A station whose own map shows no clear step does not ask. A station asked
to move to a step its map shows busy answers NAK with an alternative.

### Radio control

- `int radio_io_set_frequency(uint64_t hz)`: Hamlib `rig_set_freq` under the
  radio mutex, refused while keyed, read back and compared; HL2 through
  `hl2_set_frequency`. With a PTT method that has no tuning (serial line,
  CM108, none) the capability is not announced.
- Per-radio settle time `[qsy] settle_ms` (default 300 for HL2, 1000 for
  CAT), measured, not assumed.
- At session end Mercury returns to home (`return_home = true`): a gateway
  must be back on its published frequency, and a scanning host expects the
  radio where it left it.
- The host is told: an asynchronous `QSY <hz>` line on the control port at
  each move and at the return. It is sent only with the feature on, so an
  existing host never sees it. A host that owns the radio's CAT port must
  share it (rigctld) or the feature stays off.

### What it does not solve

- A station hidden from both ends. Both ends listening halves the problem;
  it does not remove it.
- A gateway with one receiver is deaf on home while it works a session
  elsewhere. That is no worse than today (it is busy either way), and the
  calling frequency is free for other pairs. Watching home during a session
  needs a second receiver (an SDR can have one); out of scope here.

## Steps

1. Capability negotiation (shared prerequisite, its own change).
2. `radio_io_set_frequency`, the allowed set, the map with a fake radio;
   unit tests (clock-injected, as `channel_busy` is).
3. `tests/sim`: a frequency per endpoint and a channel that couples only
   equal frequencies; phase 1 in the connect FSM; exhaustive single-loss and
   double-loss exploration of the connect exchange.
4. `specs/connect`: the traffic frequency as a variable, the property "the
   two stations are eventually on the same frequency, and on home if the
   session did not start".
5. Integration: the HL2 emulator pair with a third emulated transmitter that
   keys on home after the ACCEPT. With the feature the transfer completes at
   the clean rate; without it, measure what today does.
6. HL2 wideband occupancy map. Then phase 2: the extended control frame,
   triggers, `specs/qsy`.
7. CAT band-scope as a map source. A radio is at hand: the owner's
   IC-7300 is the one Hamlib's spectrum code was tested on (handbook,
   "Test equipment on hand"), and an RSP1B receiver can serve as a wider
   eye while developing.
8. On air, two stations, an operator at each end: the first test is manual
   (`QSY` command), triggers stay off until the false-trigger rate has been
   measured from dumps.
9. Findings document.

## Tests

- Off: frames byte-identical to trunk; no tuning call is made (a fake radio
  that fails the test on any set-frequency call).
- Mixed versions: a station with the feature and a 2.x minor-1 peer connect
  and transfer as today, either way round, and nobody moves.
- Every single loss of CALL, ACCEPT, the first poll on the traffic
  frequency, QSY-REQ, QSY-ACK: the session continues; the stations are never
  apart for longer than `hop_fail_ms` plus one poll period.
- The allowed set and the excursion are never left, including after repeated
  moves (property test over random maps).
- A busy verdict on the candidate at either end prevents the move.

## Acceptance

- Off: identical to trunk.
- Phase 1: with an interferer on home, throughput within 10 % of a clean
  channel on the bench rig; the connect time grows by no more than
  `settle_ms + listen_ms` plus one control frame.
- The model checker finds no state in which the two stations wait on
  different frequencies without a timer running.
- A findings document with the false-trigger rate of each trigger from
  recorded sessions, before any trigger is enabled by default.

## Notes for operators (to go in the user documentation)

- Listen before transmitting applies on every new frequency; this feature
  automates the listening, it does not replace the operator's
  responsibility for where the station transmits.
- The allowed set is the operator's statement of where the station may
  transmit. Rules for automatically controlled stations differ by country
  and by band.
