# What the transmitter could do: redundancy on demand, a waveform fitted to the channel, sharing the passband

Research note, 2026-11-14. It answers three questions from the fork's owner:

1. Which changes on the **transmit** side would help, including changes
   that today's stations would not understand?
2. A QR code lets its maker choose how much error correction to add. Can
   Mercury add more correction bits when the conditions call for it?
3. Can the two stations agree on a way of dividing the channel (in
   frequency, in phase) so that they share the frequency with other
   transmissions?

Everything marked *measured* was run in this repository on 2026-11-14 with
tools that are in `utils/` (commands at the end). Everything else is
arithmetic or literature and says so. **Nothing here has been on the air,
and nothing in the modem or the protocol was changed**: the measurements
use the codes and the modem as they are, with bench-only variations.

## Short answer

- **The QR-code idea is right, and it is the largest thing the transmitter
  can still give.** Today each mode sends a fixed amount of error correction
  and a frame that fails is thrown away whole. The codes Mercury already has
  can be sent *in parts* with no penalty: data plus only some of the parity
  decodes within 0.35 to 1.1 dB of an ideal code of that rate, at every
  level tried, from a rate of 0.88 downwards (*measured*). So a sender can
  start thin and add correction bits only to the frames the receiver could
  not decode:
  - above a mode's cliff, 30 to 40 % of the airtime of every frame is
    parity the channel did not need (*measured*, code level);
  - below the cliff, where today nothing is delivered, the rate falls
    smoothly instead: each instalment adds to what was already received.
    How far below is set by acquisition: 2 to 4.5 dB for the three wide
    modes, nothing for the narrow ones until proposals 03 or 21 exist
    (*measured*).
  This is proposal 28. Proposal 04 (repeat a failed frame and combine) is
  its simplest case.
- **Sharing the passband by frequency works; by phase it cannot.** A narrow
  signal of someone else (RTTY-like or FT8-like) inside a wide mode's
  passband stops the stock receiver at 3 dB *below* Mercury's own power.
  With the stretch it occupies taken out at the receiver, DATAC1 delivers
  36 to 40 of 40 frames with that signal **20 dB stronger** than Mercury's
  whole signal; with the transmitter also leaving that stretch empty, the
  cost to the link is 1.6 dB and Mercury's power inside the other signal's
  band drops by 48 dB (*measured*, with the other signal's place known to
  the bench). The receiver-only half comes first; the agreed hole is the
  wire change. This is proposal 30. Two stations with independent
  oscillators and an ionosphere between them have no common phase, so
  "phase division" between different transmitters is not available;
  section 4 says what each kind of division can and cannot do here.
- **The per-carrier noise weights matter more than this programme had
  measured.** Against a modulated narrow signal inside a *wide* mode they
  take DATAC1 from 0 to 39 of 40 and QAM16C2 from 0 to 30 of 30
  (*measured*). The earlier finding that the notch makes them redundant
  holds only for unmodulated carriers.
- **A shorter cyclic prefix is a speed option, not a sensitivity one.**
  With a 2 ms prefix instead of 6 ms a DATAC1 burst is 18 % shorter and
  delivers the same on white noise and on the CCIR Good, Moderate and Poor
  (2 ms) channels with the stock receiver (*measured*). With proposal 19 on,
  which already turns the unused prefix into signal, it costs 0.6 dB. A
  *longer* prefix does not rescue the 7 ms NVIS channel (*measured*):
  proposal 26 remains the tool there. This is proposal 29, the smallest
  of the three.
- **Nothing more to gain from clipping the QPSK modes harder**: +0.2 dB at
  equal peak power at best (*measured*). The peak-to-average table in
  section 5 also shows why the MFSK floor holds: at equal peak power it
  puts out 3.1 to 4.5 dB more than any OFDM rung.
- **For speed in good conditions** the route is the one already written
  down (proposals 15, 13, 14), with one new fact: the high code rates of
  proposal 15 need no new code matrices, because the existing one punctures
  to rates 0.79 and 0.88 within 0.4 dB of ideal (*measured*).
- **None of this needs a break in compatibility.** Each item is a
  capability two stations agree on at connect; a station without it gets
  today's behaviour. Section 7 lists the changes that would be a real
  break (a new waveform family, a new code family, self-describing frames)
  and why none is proposed.

## 1. Where the transmitter stands: every rung is 6 to 7 dB from the limit

The Shannon limit for a rate R in a bandwidth B is the SNR at which
`B log2(1 + SNR)` equals R. For each rung, with its payload rate inside a
burst, its occupied width (`docs/MODES.md`) and its measured white-noise
cliff (50 % of frames; this programme's bench tables):

| rung | bit/s | Hz | limit, SNR in 3 kHz | cliff, stock | gap | cliff with 09 + 19 | gap | Eb/N0 at the stock cliff |
|---|---|---|---|---|---|---|---|---|
| MFSK | 60 | 1000 | −18.5 | −11.2 | 7.3 | | | +5.8 |
| DATAC15 | 68 | 187 | −17.5 | −11.0 | 6.5 | −11.6 | 5.9 | +5.4 |
| DATAC4 | 87 | 250 | −16.4 | −9.4 | 7.0 | −9.8 | 6.6 | +6.0 |
| DATAC3 | 321 | 562 | −10.4 | −4.2 | 6.2 | −4.8 | 5.6 | +5.5 |
| DATAC1 | 980 | 1687 | −5.5 | +0.85 | 6.4 | −1.25 | 4.3 | +5.7 |
| DATAC17 | 1410 | 2062 | −3.8 | +3.0 | 6.8 | +0.75 | 4.6 | +6.3 |
| QAM16C2 | 3100 | 2062 | +1.0 | +7.9 | 6.9 | +6.3 | 5.3 | +7.8 |

Two things follow.

**The ladder trades bandwidth for SNR at a constant energy per bit**
(5.4 to 6.3 dB, 7.8 for 16QAM). No rung is more efficient than another;
going down the ladder only narrows the signal.

**Where the 6 to 7 dB go** (arithmetic for DATAC1, checked against the
measurements):

| loss | dB | who can recover it |
|---|---|---|
| cyclic prefix: 6 ms of every 22 ms carry no data | 1.4 | receiver (proposal 19 recovers 0.5 to 0.8 of it), or a shorter prefix |
| pilots: one symbol row in five | 1.0 | transmitter (sparser pilots), or a receiver that uses data as pilots (20) |
| the code against an ideal one of its rate (*measured*, section 2) | 0.8 | a longer or better code: little left |
| QPSK instead of an ideal alphabet at this rate | 0.2 | nothing worth doing |
| preamble, postamble, unique word | 0.2 | multi-packet bursts (13) |
| channel estimation and synchronisation, stock receiver | 2.5 to 3 | receiver (09 and 19 took 2.1 of it; the genie of proposal 20 says about 1 is left) |

So at a fixed rate the transmitter's own share is small: about 1 dB from
the prefix (which proposal 19 already claws back at the receiver) and
under 1 dB from the pilots. **The large transmit-side gains are not in
making one frame more efficient. They are in not sending what was not
needed, not wasting what was received, and not transmitting where someone
else is.** The rest of this note is about those three.

## 2. Redundancy on demand (the QR code question)

### 2.1 What exists today

- **One level per mode.** Each rung has one code at one rate: 1/2
  (DATAC1, DATAC3), 0.6 (DATAC17, QAM16C2), 1/3 (DATAC15), and two codes
  shortened to 0.30 (DATAC4) and 0.20 (DATAC16). The receiver chooses the
  rung; the amount of correction comes with it.
- **Between frames the redundancy is already on demand.** The carousel's
  erasure code (`datalink_arq/rs_erasure.h`) lets the sender add repair
  pieces until the receiver has enough, and the receiver says how many it
  still needs. That is a QR code's level chosen after the fact, at the
  granularity of whole frames.
- **Inside a frame nothing is.** A frame that fails its CRC is an erasure:
  everything it carried is dropped, however close it came. In a carousel
  session the soft combiner is switched off altogether
  (`modem/modem.c`, `modem_apply_crc_seed`: "consecutive carousel frames
  are different codewords"). Proposal 04 would bring combining back by
  asking for identical repeats.

### 2.2 Measured: the existing codes, sent in parts

`utils/ldpc_ir_sweep` takes a code of `modem/freedv/ldpc_codes.c` and the
modem's own decoder, and for each trial sends a random payload's codeword
*a part at a time*: first the data bits and a fraction of the parity, then
more parity, then bits a second time. What was not sent reaches the decoder
as "no information"; what was sent twice is added. The smallest fraction of
a codeword that decodes is the trial's result. White noise, one coded bit
per real dimension, perfect synchronisation: the code alone.

Ec/N0 (energy per sent coded bit over the noise density, dB) at which half
the frames decode, and how far that is from an ideal code of the same rate:

**`H_4096_8192_3d`, the code of DATAC1** (40 trials a point)

| sent, of a codeword | effective rate | half decode at | ideal code | behind ideal |
|---|---|---|---|---|
| 0.60 | 0.83 | +2.15 | +1.57 | 0.6 |
| 0.70 | 0.71 | +0.6 | −0.09 | 0.7 |
| 0.80 | 0.63 | −0.65 | −1.23 | 0.6 |
| 0.90 | 0.56 | −1.25 | −2.10 | 0.85 |
| **1.00 (today)** | 0.50 | −2.05 | −2.82 | 0.8 |
| 1.25 | 0.40 | −2.75 | −4.22 | 1.5 |
| 1.50 | 0.33 | −3.6 | −5.27 | 1.7 |
| 2.00 (two copies) | 0.25 | −5.05 | −6.81 | 1.8 |
| 3.00 | 0.17 | −6.8 | −8.85 | 2.05 |
| 4.00 | 0.125 | −8.0 | −10.24 | 2.2 |

**`H_16200_9720`, the code of DATAC17 and QAM16C2** (30 trials a point,
as QPSK)

| sent | effective rate | half decode at | ideal code | behind ideal |
|---|---|---|---|---|
| 0.68 | 0.88 | +2.75 | +2.40 | 0.35 |
| 0.76 | 0.79 | +1.3 | +0.92 | 0.4 |
| 0.84 | 0.71 | +0.3 | −0.09 | 0.4 |
| 0.92 | 0.65 | −0.3 | −0.88 | 0.6 |
| **1.00 (today)** | 0.60 | −1.15 | −1.54 | 0.4 |
| 1.25 | 0.48 | −1.75 | −3.09 | 1.3 |
| 1.50 | 0.40 | −2.6 | −4.22 | 1.6 |
| 2.00 | 0.30 | −4.2 | −5.85 | 1.65 |
| 3.00 | 0.20 | −5.75 | −7.95 | 2.2 |

**`H_1024_2048_4f` (DATAC3, DATAC4), 100 trials a point:** 0.60 at +2.25,
0.70 at +0.55, 0.80 at −0.5, 0.90 at −1.4, 1.00 at −2.2, 1.25 at −2.85,
1.50 at −3.7, 2.00 at −5.15, 3.00 at −6.9, 4.00 at −8.2; 0.65 to 0.75 dB
behind ideal up to a whole codeword, 1.3 to 2.1 dB beyond.

**`H_256_768_22` (DATAC15, and the control frame DATAC16), 100 trials a
point:** 0.47 at +0.7, 0.60 at −1.15, 0.73 at −2.45, 0.87 at −3.35, 1.00 at
−4.25, 1.25 at −5.1, 1.50 at −5.95, 2.00 at −7.2, 3.00 at −9.0, 4.00 at
−10.2; 0.8 to 1.1 dB behind ideal up to a whole codeword.

What the tables say:

1. **Sending less parity costs nothing extra.** Every level below a whole
   codeword is as close to an ideal code of its rate as the whole codeword
   is to an ideal code of its own. These codes were not designed for it; it
   works because their parity part is an accumulator chain
   (`encode()` in `mpdecode_core.c`), where leaving out a parity bit merges
   two checks into one.
2. **The pattern matters.** The parity has to be left out evenly spread
   (the tool sends it in bit-reversed order, so every level is a regular
   pattern and the levels are nested). Dropping the *end* of the parity
   instead gives an error floor: with 90 % sent, 76 to 97 % of frames
   decode however strong the signal, and with 80 % sent, at most 86 %
   (*measured*, `tail` option).
3. **Two copies are exactly 3.0 dB better than one, four are 6.0 dB**
   (the tool's self-check), and a repeat is 0.7 to 1.4 dB behind what an
   ideal code of the lower rate would do. Repeating is the weak way of
   lowering the rate; it is also the only one that needs no new parity.
4. **One code covers what several rungs cover today.** The DATAC1 code
   alone spans 10 dB, from +2.15 dB with 60 % sent to −8 dB with four
   copies, in steps as fine as one wants.

### 2.3 What it would save

The same runs, as airtime: coded bits sent per payload bit delivered, for
three senders. *Discard* sends whole codewords and loses a failed one
(today). *Repeat* sends whole codewords and combines failed copies
(proposal 04). *Instalments* starts at 70 % of a codeword and adds until it
decodes. *Ideal* is the limit of any code. DATAC1's code:

| Ec/N0 | ideal | discard | repeat | instalments |
|---|---|---|---|---|
| +2.0 | 1.16 | 2.00 | 2.00 | **1.40** |
| +1.0 | 1.26 | 2.00 | 2.00 | **1.40** |
| 0.0 | 1.39 | 2.00 | 2.00 | **1.60** |
| −1.0 | 1.56 | 2.05 | 2.05 | **1.83** |
| −2.0 (the cliff) | 1.77 | 3.48 | 2.85 | **2.11** |
| −2.5 | 1.91 | nothing | 4.00 | **2.49** |
| −3.0 | 2.05 | nothing | 4.00 | **2.95** |
| −4.0 | 2.41 | nothing | 4.00 | **3.51** |
| −5.0 | 2.86 | nothing | 4.80 | **4.40** |
| −6.0 | 3.43 | nothing | 6.00 | **5.70** |
| −7.0 | 4.15 | nothing | 8.10 | **8.10** |

- **Above the cliff**: 1.40 instead of 2.00, 30 % less airtime per frame,
  from 3 dB over the cliff upwards. Starting at 60 % would make it 1.20
  from 4.2 dB over the cliff. The ladder holds a rung with a margin of 2 to
  5 dB over its white-noise cliff (`ARQ_SNR_MIN_*` in `arq_protocol.h`
  against the cliffs above), so a link spends most of its time where the
  thin frame is enough.
- **At the cliff**: 2.11 against 3.48, because a frame that failed is
  completed instead of thrown away.
- **Below the cliff**: a rate that falls with the SNR (at 2 dB under,
  3.5 coded bits per payload bit, 57 % of the rung's nominal speed; at
  3 dB under, 4.4, or 45 %) where today the rung is dead and the ladder
  steps down to a mode three to four times slower.

On DATAC15's code, which the control frame also uses in a shortened form,
the same arithmetic gives 1.80 instead of 3.00 coded bits per payload bit
from about 4 dB over its cliff: a poll or an acknowledgement could be 40 %
shorter on every link that is not at the floor (to be measured on the
shortened code). Proposal 14 attacks the same cost with a different mode;
the two should be compared before either is built.

### 2.4 Where it stops: acquisition

A frame has to be found before more can be added to it. Frames acquired by
the stock receiver on white noise (`rx_bench`, *measured*):

| mode | cliff with 09 + 19 | acquired at | room under the cliff |
|---|---|---|---|
| DATAC1 | −1.25 | 40 of 40 at −3 dB, 37 at −4, 7 at −5 | about 3 dB (two copies) |
| DATAC17 | +0.75 | 30 of 30 at −3 dB, 24 at −4, 5 at −5 | about 4.5 dB (two to three copies) |
| QAM16C2 | +6.3 | 30 of 30 at +5 dB, 23 at +4, 0 at +3 | about 2 dB |
| DATAC3 | −4.8 | 83 of 100 at −4.5 dB, 37 at −5.0, 11 at −5.5 | none: it is acquisition that sets its cliff |
| DATAC4, DATAC15, DATAC16 | | "delivered = acquired" at their cliffs | none |

So the gain *under* the cliff is real for the three wide modes and waits
for proposal 03 (the receiver knows when a frame is due) or 21 for the
narrow ones. The gain *over* the cliff (thin frames) applies to every mode.

### 2.5 What it takes

The modem already sends codewords that are not whole: DATAC4, DATAC16 and
DATAC17 use *shortened* codes, where data bits both ends know are left out
(`set_data_bits_per_frame`, `ldpc_mode_specific_setup` in `interldpc.c`).
Leaving out parity is the mirror image: the receiver puts "no information"
where the transmitter sent nothing. A thin frame is a packet of fewer modem
frames (DATAC1 at 70 %: 27 instead of 38); a top-up is a packet that
carries only the bits of an earlier one that were not sent yet, and is
decoded together with what was kept of it. The protocol part is what
proposal 04 already needs: the sender keeps the frames of its last round,
the receiver keeps the soft bits of the frames that failed and asks for
more of them by position. The steps, the wire encoding and the tests are in
the specification of proposal 28.

One thing found on the way belongs to the receiver (proposal 23): the
decoder's own test for "done" counts parity checks from extrinsic signs,
and on up to 15 % of frames near a cliff it never reaches the full count
although **every bit is already right**, so the decode runs to its 100
iterations for nothing (*measured* on all four codes: for example
DATAC1's code at 3 dB over its cliff, 35 of 40 frames stop after about 9
iterations and 5 run to 100, every one of the 40 with all bits correct). A test on
the hard decisions would stop those. With bits left out it never reaches
the count at all, so proposal 28 needs that test in any case.

## 3. A waveform fitted to the channel

The receiver now measures the channel's delay profile (proposal 26) and
its Doppler and delay spread (proposal 10's telemetry). The waveform's
geometry is fixed regardless: a 6 ms cyclic prefix (4 ms on QAM16C2) and a
pilot row every fifth symbol. `rx_bench` can now open a mode with another
prefix or pilot spacing on both ends (`RX_BENCH_TCP_MS`, `RX_BENCH_NS`,
`RX_BENCH_NP`; bench only).

### 3.1 A shorter prefix (*measured*, DATAC1, burst 3.60 s instead of 4.40 s)

Delivered per 60 (the 6 ms white-noise and Poor rows are per 100, from
earlier runs, and given in per cent):

| channel | level | stock, 6 ms | stock, 2 ms | 09 + 19, 6 ms | 09 + 19, 2 ms |
|---|---|---|---|---|---|
| white noise | +1.0 dB | 78 % | 85 % | | 98 % |
| | 0.0 dB | 0 | 0 | 99 % | 97 % |
| | −0.5 dB | 0 | 0 | 97 % | 85 % |
| | −1.0 dB | 0 | 0 | 89 % | 3 % |
| CCIR Good (0.5 ms, 0.1 Hz) | +4.5 dB | 44 | 47 | 50 | 47 |
| | +2.5 dB | 32 | 36 | 46 | 41 |
| | +0.5 dB | 20 | 19 | 33 | 31 |
| CCIR Moderate (1 ms, 0.5 Hz) | +5.8 dB | 58 | 55 | 58 | 57 |
| | +3.8 dB | 41 | 37 | 54 | 52 |
| | +1.8 dB | 13 | 21 | 40 | 32 |
| CCIR Poor (2 ms, 1 Hz) | +5.8 dB | 95 % | 93 % | 95 % | 98 % |
| | +3.8 dB | 67 % | 63 % | 88 % | 87 % |
| | +1.8 dB | 16 % | 17 % | 58 % | 55 % |

- **Stock receiver: the same delivery from bursts 18 % shorter**, on every
  channel up to 2 ms of delay. That is 22 % more data per second of
  transmission.
- **With proposal 19 on, the short prefix costs about 0.6 dB on white
  noise** (cliff at −0.6 instead of −1.25 dB): 19 was already turning the
  unused prefix into signal, and there is less of it to use. In energy per
  bit the two nearly cancel. Under fading the difference is inside the
  noise of the counts, except at the lowest Moderate point.
- QAM16C2 with 2 ms instead of its 4 ms (10 % shorter): the same on white
  noise and CCIR Good; on CCIR Moderate it acquires fewer bursts (27 to 34
  of 40 against 34 to 37) and delivers fewer. Not a gain there.

So the short prefix is a way to turn spare SNR into speed on the QPSK
rungs, chosen by the receiver when the delay profile it measures is short.
It is not a way to reach further.

### 3.2 A longer prefix for NVIS: no (*measured*)

The disturbed NVIS preset has paths 7 ms apart against a 6 ms prefix. A
10 ms prefix removes the overlap between symbols. DATAC1, per 60, stock /
09 + 19 + 26:

| level | 6 ms prefix | 10 ms prefix |
|---|---|---|
| +9.8 dB | 10 / 56 | 28 / 59 |
| +7.8 dB | 9 / 54 | 24 / 57 |
| +5.8 dB | 6 / 51 | 24 / 54 |
| +3.8 dB | 0 / 32 | 12 / 41 |

The stock receiver improves and stays poor: with paths 7 ms apart the
channel turns by 2.75 radians from one carrier to the next, and its
estimate, which averages neighbouring carriers, is what fails. Proposal 26
gets nearly all of it with the standard prefix; the long prefix adds 3 to 9
frames in 60 on top of it for bursts 18 % longer. Not proposed.

A side result: with the 10 ms prefix, 09 + 19 decodes 56 of 60 frames at
−1.5 dB on white noise, where it decodes 16 of 100 with the standard
prefix, and 45 of 60 at +1.8 dB on CCIR Poor against 58 of 100. The prefix
is not wasted once proposal 19 recycles it.

### 3.3 Sparser pilots (*measured*)

A pilot row every ninth symbol instead of every fifth (DATAC1 with 19
modem frames of 9 rows: the same codeword). The burst is only 5 % shorter
(4.16 s), because the preamble and postamble are one modem frame each and
grow with it. Delivered per 60, stock / 09 + 19; the standard spacing is
from the runs above, scaled to 60 where they were per 100:

| channel | level | every 5th row | every 9th row |
|---|---|---|---|
| white noise | +1.0 dB | 47 / | 46 / 60 |
| | −1.0 dB | 0 / 53 | 0 / 52 |
| | −1.5 dB | 0 / 10 | 0 / 7 |
| CCIR Moderate (0.5 Hz) | +3.9 dB | 41 / 54 | 40 / 53 |
| | +1.9 dB | 13 / 40 | 19 / 41 |
| CCIR Poor (1 Hz) | +3.8 dB | 40 / 53 | 36 / 46 |
| | +1.8 dB | 10 / 35 | 5 / 22 |

The same on white noise and at 0.5 Hz of Doppler, worse at 1 Hz. For 5 %
of airtime it is not worth a variant of its own; with a preamble that did
not grow it would be 10 %, which could ride along with the short prefix as
a "calm channel" profile. Not proposed separately.

## 4. Sharing the passband

### 4.1 What "division" can mean between stations that do not know each other

| division | between Mercury and a foreign signal | between two Mercury links |
|---|---|---|
| **time** | exists: Mercury is half-duplex, the energy busy detector (off by default) and the NAV header keep a station off a channel in use | exists (the same means, and each decodes the other's control frames) |
| **frequency, whole channel** | proposal 24: the two stations move together to a clear frequency nearby | the same |
| **frequency, inside the passband, narrow modes** | proposal 16: the narrow rung sits where the passband is clean (*measured* in its phase 0: 60 of 60 once moved 350 Hz) | two narrow-mode links side by side in one voice channel |
| **frequency, inside the passband, wide modes** | **new, section 4.2**: leave a hole where the other signal is | possible, same means |
| **phase** | not available | not available |
| **code (spread spectrum)** | no gain in 2 to 3 kHz (issue #1, "looked at and not proposed") | no |
| **power** (decode the stronger signal, subtract it, decode the weaker) | no: the foreign signal would have to be demodulated exactly | conceivable, research only |

**Why not phase.** Two signals on the same frequency can be told apart by
phase only when they share a phase reference: that is how one QPSK
transmitter puts two bit streams on one carrier, and Mercury already does
it on every carrier. Two transmitters at different sites have independent
oscillators (tens of hertz apart on HF sets), and each path through the
ionosphere turns its signal's phase at random about once a second: that
turning *is* the fading. At the receiver the two signals have no fixed
phase relation to divide by. The same argument rules out interleaving two
stations' carriers finger-like: that needs both transmitters within a few
hertz and a few milliseconds of each other at the receiver, which cellular
networks arrange with a base station that steers every handset, and two
independent HF stations cannot.

### 4.2 Measured: someone else's narrow signal inside a wide mode

`rx_bench` adds a two-tone FSK signal to the trial: RTTY-like (45 baud,
170 Hz shift) or FT8-like (6.25 baud, 44 Hz). Its level is relative to the
**whole** power of the Mercury burst, which is spread over 1.7 kHz or more.
DATAC1 at +5 dB (4 dB over its stock cliff), per 40:

| signal, level | stock | weights (07) | 09 + 19 + weights | stretch taken out (`bs`) | `bs` + 09 + 19 + weights |
|---|---|---|---|---|---|
| RTTY-like on 1500 Hz, −10 dB | 40 | 40 | 40 | | |
| −3 dB | **0** | **39** | 40 | | |
| +3 dB | 0 | 0 | **36** | | |
| +10 dB | 0 (none acquired) | 0 | 0 | **38** | **39** |
| +20 dB | 0 | 0 | | 6 | **36** |
| +30 dB | 0 | 0 | | 0 | 0 |
| RTTY-like on 2100 Hz, −3 dB | 0 | 39 | 39 | | |
| +3 dB | 0 | 12 | 37 | 39 | 40 |
| +10 dB | 0 | 0 | 0 | 38 | 40 |
| +20 dB | 0 | 0 | | 0 | **40** |
| FT8-like on 1500 Hz, 0 dB | 0 | 39 | | 40 | 40 |
| +10 dB | 0 | 0 | | 40 | 40 |
| +20 dB | 0 | 0 | | 40 | 40 |

QAM16C2 at +14 dB, RTTY-like on 1500 Hz, per 30: at −10 dB stock 0,
weights 30; at 0 and +10 dB stock 0, weights 0, stretch taken out 30; at
+20 dB nothing.

- **A narrow signal 3 dB weaker than Mercury's whole signal stops the
  stock receiver completely** (10 dB weaker, for QAM16C2). On a shared
  band this is an everyday situation.
- **The weights of proposal 07 answer it up to about the signal's own
  power**, and to 3 dB more with 09 and 19. The automatic notch does not
  help here (it is for unmodulated carriers: against the FT8-like signal it
  delivered 0 of 40).
- **Beyond that the frame is not even acquired**, and what works is taking
  the occupied stretch out of the audio before the decoders (`bs`: the
  audio minus a band-pass around the signal, 412 Hz wide for the RTTY-like
  one, 129 Hz for the FT8-like one). The code then treats the missing
  carriers as erased. With it DATAC1 holds to +20 dB. In these runs the
  bench knows where the signal is; **finding it is not built** (the
  receiver knows when its peer is silent, so the spectrum of those gaps is
  where to look: the gap statistics of proposal 10).

**What the hole costs the link, and what the transmitter adds.** No
foreign signal, DATAC1, cliff (50 %) on white noise:

| | stock | with 09 + 19 |
|---|---|---|
| no hole | +0.85 dB | −1.25 dB |
| the receiver takes 412 Hz out (24 % of the carriers) | +3.5 dB | +1.5 dB |
| the transmitter leaves it empty as well, same average power | +2.4 dB | +0.4 dB |

For QAM16C2 the receiver-only hole moves the cliff from +7.9 to +10.5 dB
(with 09 + 19, from +6.3 to about +8.6).

So the receiver alone pays about 2.7 dB for a quarter of the passband, and
gets 1.2 dB of it back when the transmitter stops sending there and puts
that power on the carriers that are left: 1.6 dB in all. With the
transmitter's hole DATAC1 at +5 dB delivers 38 or 39 of 40 with the
RTTY-like signal 10 dB over it and 36 or 37 of 40 at 20 dB over it; at
+3 dB, with 09 + 19, 39 of 40 at 10 dB over it and 34 of 40 at 20 dB.
QAM16C2 at +14 dB: 30 of 30 with the signal level with it and 10 dB over
it, 16 of 30 at 20 dB over it.

**And the other station.** After the transmitter's band-stop the middle
292 Hz of the hole hold 48 dB less of Mercury's power than before (the
depth of the bench's filter; a band-stop *after* the modulator and its
clipper, so the clipper does not fill it in again). The other signal stops
hearing Mercury on its frequency. That is the part no receiver-side
technique can give.

## 5. Peak power

A transmitter is limited by its peak envelope power; what the receiver
gets is the average. Ratio of the two for one burst of each mode as the
modem emits it (*measured*; "envelope" is of the analytic signal, which is
what an SSB transmitter amplifies):

| mode | envelope peak over average, dB | average power at equal peak, against MFSK |
|---|---|---|
| MFSK, MFSK500 | 0.1 | 0 dB |
| DATAC16, DATAC15 | 3.2 | −3.1 dB |
| DATAC3 | 3.85 | −3.7 dB |
| DATAC4 | 4.0 | −3.9 dB |
| DATAC1 | 4.5 | −4.4 dB |
| DATAC17 | 4.7 | −4.5 dB |
| QAM16C2, unclipped | 9.7 | −9.6 dB |

- The MFSK floor is a constant-envelope signal. On white noise it is
  0.9 dB better than DATAC15 by average power on the same harness
  (`docs/MODES.md`); **by peak power that is 4.0 dB**. Any coherent mode
  proposed for the floor has to beat that.
- QAM16C2 unclipped gives away 5 dB against the QPSK rungs; proposal 02's
  switch takes 4.9 of them back.
- **Clipping the QPSK rungs harder buys nothing** (*measured*,
  `utils/qam16_papr_sweep datac17`, equal peak, 30 trials a point): drive
  4.0 instead of 2.7 lowers the ratio by 0.9 dB and moves the cliff by
  0.65 dB, a net 0.2 dB (25 of 30 against 12 of 30 at the one level that
  tells them apart); drives 6.0 and 9.0 lose more than they gain (7 and 1
  of 30 there). The shipped setting is at the optimum.

## 6. Speed in good conditions

Arithmetic from the mode table, to be measured when built. The top rung
carries 3100 bit/s inside a burst and about 2160 bit/s over a whole round
(eight 1213-byte frames in 8 × 3.70 s + 6.31 s of round overhead).

| step | factor | where |
|---|---|---|
| 16QAM at code rate 0.79 or 0.88 by sending 76 % or 68 % of the codeword | 1.32 or 1.47 | proposal 15 step 1, **without new matrices** (section 2.2; to be confirmed with the 16QAM demapper) |
| 64QAM on the same code | 1.5 | proposal 15 step 2 |
| five more carriers (2.06 to 2.4 kHz), where both radios pass them | 1.15 | proposal 15 step 3 |
| 2 ms prefix on short channels | 1.22 on the QPSK rungs (no gain measured for 16QAM, section 3.1) | proposal 29 |
| one burst per round, short polls | about 1.2 to 1.3 on the round | proposals 13 and 14 |

64QAM at rate 0.79 on 38 carriers is about 7000 bit/s inside a burst. Each step needs more SNR (on QPSK 2.5 and
3.9 dB for the two puncturing levels, more than that on 16QAM; about 5 to
6 dB for 64QAM, from the literature) and a transmitter linear enough to
show it.

## 7. Changes that would break compatibility, and why none is proposed

- **A single-carrier waveform with an equaliser** (as PACTOR-4 and the
  military serial-tone modems use) has a lower peak-to-average ratio than
  OFDM. Against the *clipped* QPSK rungs the difference is 1 to 1.5 dB at
  most, for a new modem and a new receiver.
- **A code family designed for instalments** (the 5G base graphs). The
  existing codes are already within 0.4 to 0.9 dB of ideal at every level
  measured. A new family would touch every mode for a few tenths of a dB.
- **Frames that describe themselves** (a header saying what follows). Not
  needed where the receiver chose what is sent; it costs airtime in every
  frame (issue #1).
- **Constant-envelope coherent modes for the floor.** MFSK already has the
  peak-power advantage, and coherent estimation at −11 dB is where the
  OFDM rungs lose their dB (section 1).

Everything proposed here fits the capability scheme of the handbook
("Wire format constraints"): announced at connect, used only when both ends
have it.

## 8. Recommendations

1. **Keep the per-carrier weights (07) in whatever is proposed upstream.**
   Section 4.2 is the measurement that was missing.
2. **Proposal 30, phase 1 (receiver only): find an occupied stretch in
   the peer's gaps and take it out before the decoders.** No wire change,
   works against today's stations, and it is the measured difference
   between 0 and 36 to 40 of 40 under a signal 20 dB stronger.
3. **Capability negotiation**, once (handbook), because everything below
   needs it.
4. **Proposal 04, then 28 on top of it.** 04 builds the stores and the
   request; 28 adds the thin frame and the top-up. Start with the thin
   frames of the three wide modes, where the airtime is.
5. **Proposal 30, phase 2:** the agreed hole.
6. **Proposal 29** (short prefix) with or after proposal 15: both are
   about speed with SNR to spare.
7. In proposal 23, the decoder's stop test (section 2.5).

## Caveats

- Section 2 is the code and the decoder alone: white noise, perfect
  synchronisation and channel knowledge, QPSK. Through the modem the levels
  will sit higher by each mode's estimation loss, and under fading the
  gain of adding to a failed frame should be larger than on white noise
  (a frame caught in a fade needs a little more, not a new frame), but that
  is literature, not a measurement of this modem. 30 to 100 trials a point.
- The 16QAM bit channel is not the one measured; the punctured rates for
  QAM16C2 have to be confirmed with its demapper.
- Section 3 and 4 are bench runs on white noise and two-path presets, one
  seed, 30 to 60 trials a point; differences of a few frames are noise.
  The 6 ms and 2 ms rows of section 3.1 are separate runs, not paired.
- In section 4 the foreign signal is synthetic two-tone FSK at a known
  place, steady for the whole trial. Voice, a signal that comes and goes,
  or several signals were not tried, and the detector does not exist.
- The transmitter's hole was made with the bench's 401-tap filter on the
  finished burst. In the modem it is a filter after the clipper, and the
  peak has to be restored after it; the 48 dB is that filter's depth, not
  a property of the waveform.
- Nothing was timed. A top-up means another decode of a long code; the
  stop test of section 2.5 matters for that.

## Reproducing

```sh
make -C modem/freedv libfreedvdata.a && make -C utils rx_bench qam16_papr_sweep ldpc_ir_sweep
cd utils
# section 2: a code sent in parts (code, trials, Ec/N0 from, to, step)
./ldpc_ir_sweep H_4096_8192_3d 40 -8 5 0.5
./ldpc_ir_sweep H_16200_9720   30 -7 6 0.5
./ldpc_ir_sweep H_1024_2048_4f 100 -8 5 0.5
./ldpc_ir_sweep H_256_768_22   100 -10 4 0.5
./ldpc_ir_sweep H_1024_2048_4f 100 -3 5 0.5 tail     # the careless pattern
# section 2.4: acquisition under the cliff
./rx_bench DATAC1 40 -8 -3 1 awgn stock
# section 3: prefix and pilot spacing
RX_BENCH_TCP_MS=2  ./rx_bench DATAC1 60 -1.5 1.5 0.5 awgn stock,t4f3+cp4
RX_BENCH_TCP_MS=2  ./rx_bench DATAC1 60 -36 -32 2 ccir-poor stock,t4f3+cp4
RX_BENCH_TCP_MS=10 ./rx_bench DATAC1 60 -40 -34 2 itur-mdv stock,t4f3+cp4,t4f3+cp4+pt
RX_BENCH_NS=9 RX_BENCH_NP=19 ./rx_bench DATAC1 60 -1.5 1.5 0.5 awgn stock,t4f3+cp4
# section 4: a narrow signal inside the passband (last two arguments: Hz, dB over the burst)
./rx_bench DATAC1 40 5 5 1 awgn stock,nw,t4f3+cp4,t4f3+cp4+nw 0 -99 0 0 1500 -3
./rx_bench DATAC1 40 5 5 1 awgn stock,nw,bs,bs+t4f3+cp4+nw 0 -99 0 0 1500 20
RX_BENCH_FSK_BAUD=6.25 RX_BENCH_FSK_SHIFT=44 ./rx_bench DATAC1 40 5 5 1 awgn stock,nw,nt,bs,bs+t4f3+cp4+nw 0 -99 0 0 1500 20
./rx_bench DATAC1 60 0 4 1 awgn stock,bs,t4f3+cp4,bs+t4f3+cp4 0 -99 0 0 1500 -30                              # cost of the hole
RX_BENCH_TX_HOLE=1500,412 ./rx_bench DATAC1 60 0 4 0.5 awgn stock,bs,t4f3+cp4,bs+t4f3+cp4 0 -99 0 0 1500 -30  # transmitter too
RX_BENCH_TX_HOLE=1500,412 ./rx_bench DATAC1 40 5 5 1 awgn stock,bs,bs+t4f3+cp4,bs+t4f3+cp4+nw 0 -99 0 0 1500 20
# section 5: harder clipping at equal peak
./qam16_papr_sweep datac17 30 0 5 awgn 2.7,4,6,9 0.8 0.5
```
