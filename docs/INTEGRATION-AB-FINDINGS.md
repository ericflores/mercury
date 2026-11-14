# Sessions with the receiver switches on

Two real `mercury` processes, a full ARQ session each time (call, connect,
transfer, disconnect), over a simulated channel, with the improvement
switches off and on. Measured 2026-11-13/14 with `tests/integration`.
Nothing here has been on the air.

## Question

The bench says the receiver attempts add frames. Does a session notice? Do
the switches break anything a session does today? And does any of them
lose a frame the stock receiver decodes, on the audio of a real session?

## Answer

- **Nothing breaks.** The whole integration suite passes with proposals 09,
  19 and 26 on: 25 tests, 0 failures, as with them off.
- **No frame is lost.** On recordings of what the stations heard during ten
  sessions, decoded again offline by both receivers, the switches gained 76
  frames and lost 0.
- **Where the channel is the problem, sessions finish several times
  sooner**; where it is not, nothing changes:

| channel (2 KB unless said) | stock | with the switches |
|---|---|---|
| disturbed NVIS, +10 dB (6 seeds) | 324-551 s | 88-197 s (09 + 19 + 26) |
| disturbed NVIS, +16 dB (4 seeds) | 245-644 s | 137-145 s (09 + 19 + 26) |
| CCIR Poor, +5 dB (6 seeds) | 52-153 s | 61-153 s (09 + 19 + 26): no real change |
| white noise, +3 dB (3 seeds) | 61 s | 61-62 s (all) |
| clean, 600 bytes (5 seeds) | 13 or 22 s | 22 s (all); see "The harness race" |
| carrier 3 dB over the signal, 600 bytes (2 seeds) | 173 s | 27-36 s (notch) |
| carrier 10 dB over the signal, 600 bytes (2 seeds) | **no connection** | 24-34 s (notch) |
| impulses 25 dB over the signal, 600 bytes (2 seeds) | 195-275 s | 27 s (blanker) |
| CCIR Poor with that carrier and impulses, 600 bytes (2 seeds) | **no connection** | 48 s (all) |

**Sessions are not paired** (see "The harness race"): the same
configuration on the same seed can take 13 s or 22 s, and on NVIS anything
from 63 s to 530 s. The table says "several times sooner" and "no change";
it does not support a figure such as "3.2 times".

## Method

- `TestMercuryARQTransfer`, the lockstep transport
  (`MERCURY_TEST_TRANSPORT=sock`, paced; never `MERCURY_TEST_SOCK_FAST`,
  which outruns the decoders), the Watterson engine, a pinned channel seed,
  incompressible payload. Each configuration is a separate run of the same
  test with the environment variables of the switches set or not.
- "All" is `MERCURY_RX_ALL=1` (09 auto, 19 at 4, 26, 07 weights and notch,
  08, 20, 23's early termination), or the same set named one by one.
- Time is the test's own duration: start-up, connect, transfer, disconnect.
- A run is kept only if the simulator reports the stations kept up (no
  more than a few late blocks) and no frame was decoded late.
- Interference: `MERCURY_CH_TONE` and `MERCURY_CH_IMPULSES`, added to what
  both stations hear after the channel. Levels are relative to the keyed
  signal as the simulator measured it.
- The exact comparison: `MERCURY_TEST_RX_DUMP=1` records what each
  station's decoders were handed; `utils/rx_replay FILE
  stock,auto+cp4+pt arq <seed>` decodes that audio with both receivers and
  matches the frames.

## Results

### The suite

`MERCURY_CHANEST=auto MERCURY_CP_RECYCLE=4 MERCURY_PROFILE_TRACK=1 make
integration-test`: 25 passed, 0 failed, 1 skipped (the one that is always
skipped), 651 s. The suite's channels are
nearly clean, so this says the switches do no harm; it does not exercise
them.

### Disturbed NVIS (ITU-R F.1487 mid-latitude disturbed, 7 ms, 1 Hz), 2 KB

Seconds per session; "rescued" is the number of frames an optional attempt
delivered (the `rx decode path` lines of both stations' logs).

| seed | +10 dB stock | +10 dB, 09 + 19 + 26 | rescued |
|---|---|---|---|
| 1 | 383 | 103 | 2 |
| 2 | 551 | 134 | 9 |
| 3 | 325 | 154 | 13 |
| 4 | 324 | 118 | 12 |
| 5 | 538 | 197 | 13 |
| 6 | 500 | 88 | 6 |

| seed | +16 dB stock | +16 dB, 09 + 19 + 26 | rescued |
|---|---|---|---|
| 1 | 245 | 145 | 8 |
| 2 | 644 | 137 | 11 |
| 3 | 519 | 140 | 9 |
| 4 | 455 | 137 | 8 |

More SNR does not help the stock receiver here, as on the bench
(`docs/PROFILE-TRACKING-FINDINGS.md`).

The same channel at +10 dB, 09 + 19 + 26 against everything
(`MERCURY_RX_ALL=1`), a later build, six other seeds:

| seed | 09 + 19 + 26 | all |
|---|---|---|
| 11 | 150 | 149 |
| 12 | 112 | 146 |
| 13 | 530 | 147 |
| 14 | 153 | 127 |
| 15 | 377 | 63 |
| 16 | 134 | 183 |

An earlier batch of three, run while the machine was saturated with
benches, gave 103, 203 and 257 s for "all" and looked like a penalty; this
one, and the replay below, say it was not. It also shows how far two runs
of one configuration can be apart (112 to 530 s).

### What the stations heard, decoded again (`utils/rx_replay`)

The five +10 dB sessions run with the switches **off**, both stations'
recordings, every OFDM mode's decoder. Frames delivered:

| recording | stock | 09 + 19 + 26 | gained | lost |
|---|---|---|---|---|
| session 1, receiving station | 21 | 33 | 12 | 0 |
| session 1, sending station | 12 | 12 | 0 | 0 |
| session 2, receiving station | 18 | 32 | 14 | 0 |
| session 2, sending station | 21 | 22 | 1 | 0 |
| session 3, receiving station | 25 | 55 | 30 | 0 |
| session 3, sending station | 15 | 16 | 1 | 0 |
| session 4, receiving station | 30 | 45 | 15 | 0 |
| session 4, sending station | 16 | 16 | 0 | 0 |
| session 5, receiving station | 55 | 58 | 3 | 0 |
| session 5, sending station | 13 | 13 | 0 | 0 |
| **total** | **226** | **302** | **76** | **0** |

The sending station hears control frames (3 carriers), where there is
little to gain; the receiving station hears the payload.

Each newer feature added alone to 09 + 19 + 26, on three of those
recordings (payload modes only): early termination, the noise weights, the
blanker at 6 and the notch change nothing (22, 52 and 39 frames each time);
decision-directed estimation adds one frame on one recording (53). None
loses a frame.

### CCIR Poor (2 ms, 1 Hz) at +5 dB, 2 KB

| seed | stock | 09 + 19 + 26 | rescued |
|---|---|---|---|
| 1 | 138 | 64 | 2 |
| 2 | 61 | 62 | 0 |
| 3 | 153 | 153 | 0 |
| 4 | 61 | 62 | 0 |
| 5 | 52 | 61 | 0 |
| 6 | 72 | 61 | 1 |

At this level a session hardly ever loses a frame, so there is nothing to
rescue; the differences are the harness race and one slow connect that
both configurations of seed 3 had.

### White noise at +3 dB, 2 KB, and a clean channel, 600 bytes

| | stock | all |
|---|---|---|
| white noise, seeds 1-3 | 61.2, 61.3, 61.1 s | 61.6, 61.4, 61.6 s |
| clean, seeds 1-5 | 12.7, 21.6, 12.7, 21.7, 21.7 s | 21.7, 21.8, 21.7, 21.8, 21.7 s |

### A carrier and impulses, 600 bytes

| channel | stock | switch | with it |
|---|---|---|---|
| +20 dB, carrier on 1650 Hz 3 dB over the signal | 173, 173 s | notch | 36, 27 s |
| the same | | notch and weights | 36, 27 s |
| +20 dB, carrier on 1625 Hz 10 dB over the signal | no connection, twice | notch | 24, 34 s |
| +25 dB, five 2 ms bursts a second 25 dB over the signal | 275, 195 s | blanker at 6 | 27, 27 s |
| CCIR Poor, +20 dB, that +10 dB carrier and three bursts a second | no connection, twice | all | 48, 48 s |

"No connection": the test gave up after 110 s without a CONNECTED.

## The harness race

The test writes the payload about a second after it reads CONNECTED, and
the caller's first turn comes at the same moment. In one clean run the
payload reached the modem 1 ms before the turn and went out in the first
keydown (12.7 s); in the others it missed it, the first keydown was an
empty poll, and the data went a round later (21.7 s). The connect itself
lands on a 20 ms block boundary that differs from run to run, so a marginal
CALL decodes in one run and not in the next (a 12 s difference). Neither
has anything to do with the switches, and both are why the comparison that
counts is the replay of recordings. Handbook pitfall 19.

## Caveats

- A simulated channel, two-path presets, synthetic interference; nothing
  on the air.
- Few seeds (2 to 6 a cell), and runs of one configuration differ widely.
- Short transfers (600 bytes to 2 KB): the ladder has little time to climb,
  so the effect on throughput at length is not measured.
- The machine ran other benches at the same time; runs with late blocks
  were few (0 to 4 blocks, under 75 ms) and no frame was decoded late in
  any run reported here, but a Raspberry Pi has not been tried, and there
  the extra decode attempts cost more.
- The sessions with the blanker used a threshold of 6; the recommended
  value is now 8 (`docs/BLANKER-FINDINGS.md`), which `MERCURY_RX_ALL` uses
  from the NVIS seeds 11-16 on.
- False-accept controls (noise only, another session's frames) are bench
  runs: `utils/rx_bench` with channel `noise` and `RX_BENCH_WRONG_SEED=1`.
  Their results are added to this document when run.

## Reproducing

```sh
make && make -C utils rx_replay watterson_test
cd tests/integration
E="MERCURY_CH_ENGINE=watterson MERCURY_TEST_TRANSPORT=sock MERCURY_TEST_PAYLOAD_RANDOM=1 MERCURY_TEST_SEED=3"
# disturbed NVIS at +10 dB, 2 KB, off and on, with recordings
env $E MERCURY_CH_FADING=itur-mdv MERCURY_CH_NO=-25 MERCURY_TEST_PAYLOAD_KB=2 \
    MERCURY_TEST_LOGDIR=/tmp/off MERCURY_TEST_RX_DUMP=1 \
    go test -count=1 -run '^TestMercuryARQTransfer$' -v ./...
env $E MERCURY_CH_FADING=itur-mdv MERCURY_CH_NO=-25 MERCURY_TEST_PAYLOAD_KB=2 \
    MERCURY_RX_ALL=1 MERCURY_TEST_LOGDIR=/tmp/on \
    go test -count=1 -run '^TestMercuryARQTransfer$' -v ./...
grep -c 'rx decode path' /tmp/on/*.timing.log
# what station B heard with the switches off, decoded by both receivers
seed=$(grep -ho 'session CRC seed 0x[0-9a-f]*' /tmp/off/*.timing.log | head -1 | sed 's/.*0x//')
../../utils/rx_replay /tmp/off/B.rx.s16 stock,auto+cp4+pt arq $seed
# a carrier 10 dB over the signal
env $E MERCURY_CH_NO=-45 MERCURY_CH_GAIN=0.3 MERCURY_CH_TONE=1625,-10 MERCURY_TEST_PAYLOAD_B=600 \
    MERCURY_NOTCH=1 go test -count=1 -run '^TestMercuryARQTransfer$' -v ./...
```
