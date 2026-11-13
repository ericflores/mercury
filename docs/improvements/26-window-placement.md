# 26 — Place and track the FFT window from the delay profile (issue #26)

**Kind:** receiver only. No wire change.
**Depends on:** 09 and 19 (the list of decode attempts and
`decode_packet_from_syms()`); 23 (each attempt costs a decode).
**Switch:** `MERCURY_PROFILE_TRACK` / `[audio] rx_profile_track`, default
off.

## Goal

Make long multipath decodable. On a channel whose delay spread is near or
beyond the cyclic prefix (disturbed NVIS: 7 ms against a 6 ms prefix),
today's receiver delivers under a third of DATAC1 frames **at any SNR** up
to +18 dB. The prototype below delivers half of them at +4 dB, 82 % at
+6 dB and 97-100 % from +8 dB, on the same samples.

## Facts

- `ofdm_demod_core()` (`modem/freedv/ofdm.c`), after the per-frame timing
  update:

  ```c
  ofdm->sample_point = max(ofdm->timing_est + 4, ofdm->sample_point);
  ofdm->sample_point = min(ofdm->timing_est + ofdm->ncp - 4, ofdm->sample_point);
  ```

  and every row's FFT starts at `... + 1 + ofdm->sample_point`. A new burst
  starts with `sample_point = timing_est = 0`
  (`ofdm_sync_search_burst`), so the window starts **about 4 samples into
  the cyclic prefix of the path the detector found**. Measured on white
  noise: the lag of that path (window start minus the start of its useful
  part) is −42 to −43 samples; `ncp` is 48.
- So the stock window tolerates a path up to about 5.5 ms **earlier** than
  the detected one and only 0.5 ms **later**. A later path beyond that puts
  its previous symbol inside the window.
- The per-frame tracker is `est_timing()` over `ftwindowwidth` samples
  around the current estimate (80 for the data modes, `ofdm_mode.c`: ±5 ms).
  With a moderate spread it sometimes settles on the other path and the two
  lines above then slide the window to a position that suits both; that is
  why CCIR Poor (2 ms) works.
- **With 7 ms it loses the burst.** When the path it follows fades and the
  signal is carried by a path more than 5 ms away, the correlator has
  nothing to hold. `timing_est` wanders, each excursion past
  `samplespersymbol / 8` shifts the frame grid by a quarter symbol (`nin`),
  and the rest of the burst is noise to the demodulator whatever the SNR.
  A trace of one such burst is in `prototype/lab3/README.md`.
- Proposal 09 gained nothing on that channel
  (`docs/CHANNEL-ESTIMATION-FINDINGS.md`): the limit there was never noise
  in the reference.
- The receive buffer keeps `(np + 2)` modem frames of history for bursts
  acquired on their postamble (`nrxbufhistory`), and the demodulator already
  replays a packet from it (`rxbufst` rewound, `nin = 0` while samples are
  in the buffer).

## Prototype result

Lab hooks in a scratch tree (`prototype/lab3/`): one variant of the receiver
whose window, and frame grid, are driven by the tracker of the Design
section, against stock on the same samples. ITU-R F.1487 mid-latitude
disturbed NVIS (7 ms, 1 Hz). Delivered frames; every row acquired all bursts
but one or two.

DATAC1 (27 carriers):

| SNR3k (measured) | trials | stock | with the tracker | tracker and 09's `t4` |
|---|---|---|---|---|
| +3.8 | 60 | **0** | **31** | **34** |
| +5.8 | 60 | **4** | **49** | **52** |
| +7.8 | 60 | **12** | **58** | **58** |
| +13.9 | 40 | **8** | **40** | 40 |
| +17.9 | 40 | **12** | **40** | 40 |

DATAC3 (9 carriers):

| SNR3k (measured) | trials | stock | with the tracker | tracker and `t4` |
|---|---|---|---|---|
| +8.4 | 60 | **27** | **57** | 57 |
| +12.4 | 60 | **28** | **57** | 57 |

Stock does not improve with SNR: it is limited by another symbol inside the
window and by losing the burst, not by noise. In every row the count of
frames delivered by any variant equals the last column: no frame that stock
delivered was missed by the tracker with `t4`.

Controls, tracker against stock as the **only** receiver (the proposal uses
it as a later attempt, which cannot lose a frame; these rows say what it
would do if it ran first):

| case | trials | stock | tracker |
|---|---|---|---|
| DATAC1, white noise, +0.5 dB | 200 | 3 | 5 |
| DATAC1, white noise, +1.0 dB | 200 | 165 | 170 |
| DATAC3, white noise, −4.5 dB | 100 | 12 | 9 |
| DATAC3, white noise, −4.0 dB | 100 | 76 | 71 |
| DATAC1, CCIR Poor (2 ms), +3.8 dB | 60 | 42 | 42 |
| DATAC1, CCIR Poor, +1.8 dB | 60 | 9 | 7 |
| the same two rows with 09's `t4f3` | 60 | 51, 28 | 51, 30 |
| QAM16C2, CCIR Poor, +13.5 dB | 40 | 33 | 34 |
| QAM16C2, CCIR Poor, +9.5 dB | 40 | 9 | 8 |

One seed, one two-path preset, bench only. A real NVIS path has more than
two rays and delays that move; nothing here has been on the air.

Also measured and **not** worth pursuing (recorded so nobody repeats it):

- a fixed window at the end of the prefix: better than stock on NVIS (21
  against 12 of 60) but **worse** than stock on CCIR Poor as the only
  attempt (31 against 42); in the middle of the prefix, worse everywhere;
- choosing the window each frame from the profile but leaving the stock
  tracker in charge of the frame grid: 34 of 60 where the full tracker
  delivers 58; the bursts it still loses are the ones where the
  stock tracker walks away;
- a profile taken as local maxima of a tapered transform: fine with 27
  carriers, useless with 9 (the main lobe covers half of the lags);
- giving each data row its own channel estimate by interpolating between
  pilot rows (linear, Catmull-Rom over four pilot rows as modem73 does, or
  proposal 09's four-pilot weights with a time slope): no change on CCIR
  Poor or NVIS at 1 Hz Doppler (51 against 51 of 60, 14 against 13).

## Design

### 1. Paths, per modem frame, by matching pursuit on the pilot rows

Every pilot row has a known symbol on every carrier. Through a window that
starts `L` samples after the start of a path's useful part (`L` negative:
inside that path's prefix), carrier `i` of the row is rotated by
`exp(+j·2π·i·L/m)`. For the pilot rows of the frame (previous, this, next,
future; skip the previous on a burst's first frame and the future on its
last), with `H_r(i) = row_r[i] · conj(pilot[i])`:

```
E = Σ_r Σ_i |H_r(i)|²
repeat up to 3 times:
    for d in −64 … 63:  v(d) = Σ_r | Σ_i H_r(i) · exp(−j·2π·i·d/m) |²
    d* = argmax v;  explained = v(d*) / nc
    stop if explained < K · E / nc            (K = 4: what a noise lag gives is E/nc)
    stop if explained < 0.1 · the first path's
    stop if d* is within 4 lags of a path already taken
    take (d*, explained)
    for each r:  a = (1/nc) Σ_i H_r(i) · exp(−j·2π·i·d*/m);   H_r(i) −= a · exp(+j·2π·i·d*/m)
    E = Σ_r Σ_i |H_r(i)|²
```

Precompute `exp(−j·2π·i·d/m)` for the mode (128 × `nc` values); the
prototype calls `cos`/`sin` in the loop and is slow for it.

Matching pursuit, not a transform with a taper: with 9 carriers a tapered
main lobe covers half of the 128 lags and two paths 7 ms apart merge.

### 2. Tracks: remembering paths, and which multiple of 16 ms

Lags are known modulo `m` (128 samples). Keep up to 8 tracks
`(lag, weight, age)`, lags relative to the chain's own window:

- a detected path within 10 samples of a track, trying lag, lag − 128 and
  lag + 128, is that track: update its lag, weight ← half old, half new,
  age ← 0;
- otherwise it is a new track, placed at the candidate nearest the weighted
  centroid of the tracks (weights faded by 0.85 per frame of age);
- with no tracks at all (a burst's first frame): cut the circle at the
  largest gap between the detected lags, which gives the smallest spread,
  then choose the multiple of 128 that puts the centroid nearest `−ncp/2`
  (the stock detector leaves its own path between −44 and −4);
- age every track each frame; drop it after 25 frames unseen. A faded path
  comes back where it was, so the window keeps a place for it.

### 3. Placement

For a shift `Δ` of the window, a path at lag `L` contributes
`isi(L+Δ) = max(0, L+Δ+2) + max(0, −ncp+2−(L+Δ))` samples of another symbol
(2 samples of margin each side). Choose the `Δ` in −64…64 that minimises
`Σ weight · 0.85^age · isi(lag+Δ)` over the tracks, and move only if that is
strictly better than staying. A single path that sits inside the prefix is
never moved: for DATAC1 on white noise the prototype's window is the stock
window sample for sample. At DATAC3's cliff (−4 dB, 9 carriers) the
detection sometimes takes noise for a second path and moves it, which is
the 71 against 76 of the control table; raise `K` with few carriers, or
require a path in two consecutive frames, and measure (step 7).

### 4. The chain: a replay of the burst that failed

The tracker must own its window **and** its frame grid; sharing the stock
tracker's grid is the variant that lost a third of the gain. And the stock
attempt must stay untouched. Both are met by demodulating a failed burst a
second time from the samples the receiver still holds:

1. On a burst's first modem frame save what the demodulator starts from:
   the absolute sample count at `rxbufst`, `timing_est`, `sample_point`,
   `foff_est_hz`, and the other fields `ofdm_demod_core()` carries from
   frame to frame (list them when writing the code; `mean_amp`, the
   frequency-tracking state, `clock_offset_counter`).
2. When the packet is complete and every earlier attempt has failed its
   CRC, or when sync is dropped on the unique word after the first frame:
   work on a scratch copy of the `struct OFDM` (its own `rx_sym`, `rx_np`,
   `rx_amp`; the sample buffer shared, read-only), restore the saved fields,
   set its `rxbufst` to where that first frame now is in the buffer, and run
   `np` frames with
   - no `est_timing()` call: after each frame's path detection,
     `sample_point = w` and `timing_est = w − ncp/2`, so the existing
     quarter-symbol grid shift keeps the grid centred on the tracker's
     window, not on a correlator;
   - everything else as stock (frequency tracking from its own pilot rows,
     the per-carrier estimate, proposal 09's `t4` if on; **not** the
     smoothing across carriers when more than one path is tracked).
3. Collect the corrected symbols and amplitudes of the `np` frames and hand
   them to `decode_packet_from_syms()` (it exists since proposal 19), behind
   the same parity gate and CRC. `freedv_get_rx_decode_path()` returns 4.
   The attempt decodes from the `t4` estimate (time only) of the replayed
   rows whatever `rx_chanest` is set to: in the prototype the union of all
   variants equalled that column in every row.

Facts the replay has to respect (checked in the code, 2026-11-13):

- **Where.** `freedv_comp_short_rx_ofdm()` (`freedv_700.c`) demodulates the
  frame, then, when `ofdm->modem_frame == np - 1`, runs the decode attempts,
  and only **after** that calls `ofdm_sync_state_machine()`, which at the
  end of a burst zeroes the whole `rxbuf`. So the replay runs with the other
  attempts, while the samples are still there; later is too late.
- **Finding the first frame again.** Samples enter by shifting `rxbuf` left
  by `nin` (four places: `ofdm_demod`, `ofdm_demod_shorts`,
  `ofdm_sync_search`, `ofdm_sync_search_shorts`). Keep a running count of
  samples shifted in. On the first frame record
  `count − nrxbuf + rxbufst`; at replay time that frame is at
  `recorded − (count − nrxbuf)`. This also covers a burst acquired on its
  postamble (`rxbufst` rewound, `nin = 0`). If the result is negative the
  frame has left the buffer: no replay.
- **The down-conversion's phase reference is the buffer index**
  (`work[k] = rxbuf[j] * cmplxconj(woff_est * j)`), so a replayed frame has
  a different common phase than when it was first demodulated. The pilots
  remove it, but only to within rounding: a replay with the stock tracker
  reproduces the stock symbols to about one part in 10^4, not bit for bit.
- **The receive band-pass** (`rx_bpf_en`, narrow modes only) filters new
  samples in place as they arrive. A replay must not filter again.
- **The end of the burst may not be in the buffer yet.** When the stock
  tracker has walked its grid forwards, it declares the packet complete
  before the last symbols have arrived (the trace in the prototype's README
  ends 1.5 symbols early). A replayed frame whose rows (this pilot, the
  data rows, the next pilot) would read past the end of `rxbuf` is erased
  whole: amplitudes zero, so its soft bits are zero. The future pilot row
  is optional, as on a last frame. One frame in 38 is 2.6 % of the
  codeword.
- **Soft-bit scale.** `symbols_to_llrs()` takes `mean_amp`; give it the
  replay chain's own, not the live demodulator's (pitfall 3).
- **First and last frame.** The estimator and the tracker test
  `sync_state == trial && sync_counter == 0` and
  `sync_state == synced && modem_frame == np - 1`. In a replay those fields
  belong to the loop; put the two tests behind small functions that a
  replay can answer for itself.
- **State.** The tracker's tracks and window live in `struct OFDM`, not in
  statics as in the prototype: several instances run in different threads.

Cost on a failed burst: `np · (ns + 3 + 4)` FFTs, the path search (3 × 128
lags × 4 rows × `nc` multiply-adds per frame with the table) and one LDPC
decode. Measure it for the budget of proposal 23. It is skipped, at no
cost, when the first frame's detection shows a single path inside the
prefix and the stock tracker's estimate never left its range during the
burst (then the replay would reproduce the stock rows).

### 5. Scope

- Modes with at least 9 carriers (DATAC3, DATAC1, DATAC17, QAM16C2, DATAC0,
  DATAC13). With 3 or 4 carriers there is nothing to resolve paths with;
  those modes are left alone.
- Spreads under 8 ms (half of `m`): beyond that the "smallest spread" rule
  picks the wrong multiple.
- Proposal 19 on the replayed rows: the clean part of the prefix is known
  from the tracks (from the last path's prefix start to the first path's
  useful start) and replaces 19's fixed guard. Both proposals are one
  mechanism: a prefix sample and its twin one symbol length away carry the
  same signal; both clean, average them (19); one of them inside another
  symbol of some path, take the clean one (a shifted window is that choice
  for a run of samples).

### 6. Later, each its own measured change

- Run the tracker live and first, not as a replay, on a session whose
  previous bursts showed a long profile: saves the failed stock decode and
  the replay.
- In a replay the whole burst is known: smooth the tracks forwards and
  backwards.
- Tell the carousel: a session on a long-profile channel may deserve rungs
  the ladder now avoids there. Ladder constants are not touched in this
  proposal.

## Steps

1. Port the lab hooks (`prototype/lab3/lab3-prototype.diff`: `mx_detect`,
   `mx_place`) as `ofdm_profile_paths()` and `ofdm_profile_place()`, with
   the precomputed table; unit-test them on synthetic rows (one path, two
   paths 56 samples apart with either one stronger, a path that disappears
   for 10 frames, noise only).
2. Save and restore of the demodulator's starting state; the replay loop on
   a scratch copy; check first that a replay with the tracker **disabled**
   (stock timing) reproduces the stock symbols of a clean burst to within
   rounding. That test proves the rewind arithmetic before any new
   behaviour depends on it.
3. The attempt in `freedv_700.c`, the switch, `rx_bench` token `pt`.
4. Unit test `tests/modem/test_ofdm_profile.c` (below).
5. Bench, 100 trials a cell: DATAC3, DATAC1, DATAC17, QAM16C2 on
   `itur-mdv`; all of them on `ccir-poor`, `ccir-moderate`, `itur-lm` and
   white noise as neutrality controls. Report acquired, delivered per
   attempt, and which attempt delivered.
6. A frequency offset (±20 Hz, ±50 Hz) and a sample-clock offset (resample
   the burst by ±50 ppm): the tracker must follow a slow drift of the lags.
7. Sweeps on `itur-mdv` and `ccir-poor`: `K` (3, 4, 6), the 0.1 floor, the
   track age limit, the margin.
8. With 09 (`t4`) and with 19 on the replayed rows.
9. Carousel level: `tests/sim` and the integration test under the NVIS
   preset, throughput with and without.
10. Findings document `docs/PROFILE-TRACKING-FINDINGS.md`.

## Tests

- Off: decoded bytes and status sequence identical to trunk.
- Replay with the tracker disabled equals the stock symbols to within
  rounding (step 2).
- The live demodulator is untouched by a replay: its tracking fields and
  the stock, 09 and 19 symbol buffers are identical, call by call, to a
  receiver with the feature off.
- A synthetic two-path burst (the second path a copy delayed 7 ms, equal
  power, no fading, +10 dB) fails with stock and decodes with the feature;
  the same with the paths' powers alternating every 0.5 s.
- Nothing lost: on seeded cliff runs of every mode on white noise and
  `ccir-poor`, every frame stock delivers is delivered with the feature on
  (it is a later attempt, so this is a check of the plumbing).
- Noise only: no frame accepted in 1000 windows per mode.
- ASan/UBSan clean: the replay indexes a buffer backwards.

## Acceptance

- Off: identical to trunk.
- DATAC1 on `itur-mdv` at +8 dB: at least three times stock's delivered
  frames, 100 trials; DATAC3 at +8 dB: at least 1.5 times.
- No loss on any preset.
- Time per failed burst reported for the budget of proposal 23.

## Open questions

- More than two paths, and spreads between 8 and 16 ms.
- Doppler spreads of 10 Hz and more (`itur-ld`, `itur-hd`): the tracks'
  half-old-half-new weighting may be too slow or too fast there.
- Acquisition is untouched: which path the detector takes is left as it is,
  and a burst the detector misses is still missed.
- The 3- and 4-carrier modes on such a channel: measure what stock does
  there before deciding whether anything is needed.
- The genie figures of the research note for NVIS (proposal 20's ceiling)
  were taken with the stock window. They say nothing about what is left
  to gain after this proposal; measure again on the replayed rows.
