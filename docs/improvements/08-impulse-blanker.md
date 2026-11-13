# 08 — Impulse-noise blanker (issue #8)

**Kind:** receiver only. No wire change. **Depends on:** 00-bench.
**Switch:** `MERCURY_BLANKER=<k>` / `[audio] rx_blanker = <k>`; 0 or unset =
off (default).

## Goal

Zero the few samples hit by strong short noise bursts before any decoder
sees them, for all planes at once.

## Facts

- Captured audio reaches the decoders in `rx_thread()` in `modem/modem.c`.
  Around line 3434-3438 the int32 capture is converted to `capture_i16`,
  then `rx_dump_chunk(capture_i16, chunk_samples)` writes the optional dump,
  then the chunk is copied ("teed") to each worker's ring (control, payload,
  listeners). **Insert the blanker between the dump and the tee**, so the
  dump stays raw (replay can then test the blanker offline) and every plane
  gets blanked audio.
- There is no AGC, limiter or blanker anywhere in this path today.
- OFDM's own peaks: PAPR is about 7 dB for the clipped modes and 11.4 dB for
  QAM16C2 (`docs/CLIPPED-QAM16C2-FINDINGS.md`). For a Gaussian-like signal
  the mean of |x| is about 0.8 × rms, so legitimate peaks reach about 2.8 ×
  (clipped modes) to 4.6 × (QAM16C2) the mean of |x|. A threshold below
  about 6 will blank the signal's own peaks at high SNR.

## Prototype result

DATAC1, AWGN +5 dB, 3 impulses per second of 2 ms: at +25 dB over the signal
rms stock 0/50, blanker 50/50 (thresholds 4 and 6 both); at +35 dB the same;
at +15 dB 49/50 against 50/50 (`prototype/results/H_impulse.txt`).

## Design

New module `modem/rx_blanker.{c,h}` (no dependencies, unit-testable):

```c
typedef struct {
    float    k;          /* threshold, multiples of the tracked level; 0 = off */
    float    level;      /* leaky mean of |x| over un-blanked samples          */
    int      win;        /* time constant in samples (2048 = 256 ms at 8 kHz)  */
    int      hold;       /* samples still to blank after a trigger             */
    int      hold_len;   /* samples blanked after each trigger (8 = 1 ms)      */
    uint64_t seen, blanked;
    uint32_t sec_seen, sec_blanked;   /* for the per-second safety cap */
    bool     suspended;  /* cap hit: pass audio through until the next second  */
} rx_blanker_t;

void rx_blanker_init(rx_blanker_t *b, float k, int fs);
int  rx_blanker_process(rx_blanker_t *b, int16_t *x, int n);   /* in place; returns samples blanked */
```

Algorithm per sample:

1. `a = |x|`. If `hold > 0`: zero it, `hold--`, continue (do not update the
   level).
2. If `level` is initialised and `a > k * level`: zero it, `hold =
   hold_len`, count, continue.
3. Otherwise `level += (a - level) / win`.

Initialise `level` from the mean of the first `win` samples.

Safety cap: if more than 5 % of the samples of the current second were
blanked, set `suspended` for the rest of that second and log once. This
protects against a mis-set threshold and against a signal much stronger than
the level estimate at the start of a burst.

Start-of-burst behaviour matters: the level tracks noise during silence, then
a strong burst arrives. With `win` = 256 ms a +20 dB burst over the noise
would be blanked wholesale at its start. Therefore make the tracker
asymmetric: rise fast (time constant 2 ms) when `a` exceeds the level for
more than `rise_len` consecutive samples (16 = 2 ms), fall slowly (256 ms).
An impulse shorter than 2 ms is blanked; a burst is adopted as the new level
within 2 ms and loses at most its first 2 ms. Whether that is harmless must
be shown by the neutrality test below (`utils/resync_gap_probe.c` records
that clipping 100 ms or more off a burst's start kills every mode; nothing is
recorded for 2 ms). This is the part the prototype did not exercise: its
noise floor was present throughout, so the level never had to jump.

Plumbing: `static rx_blanker_t g_blanker;` in `modem.c`,
`modem_set_rx_blanker(float k)`, applied from `mercury_engine.c` (ini) and
overridden by the environment variable. Recommended value when enabled: 6.

Statistics: every 10 s, if anything was blanked, one `HLOGI("modem-rx", ...)`
line with the blanked fraction; expose the fraction in the status snapshot
for spec 10.

## Steps

1. Module and unit tests.
2. Insert in `rx_thread`; switch; log line.
3. `rx_bench --impulse` and `--periodic` across all OFDM rungs, DATAC16 and
   both MFSK floors (MFSK is fed through `modem_mfsk`; add an MFSK path to
   the bench or use `utils/mfsk_rx_probe` with the same impairment code).
4. Neutrality control: every mode, clean AWGN from the cliff to +30 dB, and a
   burst-after-silence case, threshold 4, 6, 8.
5. Blanked-sample map to the decoder (phase 2): pass the count of blanked
   samples per OFDM symbol so spec 07's per-frame weights can lower the
   confidence of symbols that lost samples.
6. Periodic mode (phase 3): estimate the impulse period by autocorrelating
   the trigger times; when a stable 100 or 120 Hz comb is found, blank the
   predicted positions even when individual impulses are below threshold.
7. Findings document `docs/BLANKER-FINDINGS.md`; a paragraph in the setup
   documentation recommending fast or no AGC on the radio for data.

## Tests (`tests/modem/test_rx_blanker.c`)

- `test_off_is_passthrough`: `k = 0` leaves the buffer untouched.
- `test_clean_signal_untouched`: a generated burst of each OFDM mode
  (including unclipped QAM16C2) at full scale and at −30 dBFS, preceded by
  1 s of low noise, with `k = 6`: fewer than 0.01 % of burst samples blanked
  after the first 2 ms.
- `test_impulses_blanked`: 2 ms impulses 25 dB above the signal: at least
  95 % of impulse samples zeroed, fewer than 0.1 % of other samples.
- `test_cap_suspends`: white noise with `k = 1` trips the 5 % cap and passes
  audio through.
- `test_level_not_captured`: after 50 impulses the tracked level is within
  1 dB of the level without them.

## Acceptance

- Off: bit-identical audio to the decoders.
- Neutrality: no cell loses more than 1 frame in 100 with `k = 6`.
- Impulses (3/s, 2 ms, +25 dB): at least 95 % delivered where stock delivers
  under 10 %, for DATAC1, DATAC17 and MFSK.

## Open questions

- Real static crashes last tens of milliseconds and arrive already shaped by
  the radio's filter and AGC. Collect dumps (`MERCURY_RX_DUMP`) from the air
  and replay them (`rx_replay`) before claiming a field benefit.
- With a narrow receive filter (BW500 stations) impulses are stretched to
  2 ms or more; `hold_len` may need to follow the session bandwidth.
