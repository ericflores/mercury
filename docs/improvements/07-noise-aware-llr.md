# 07 — Noise-aware LLRs (issue #7)

**Kind:** receiver only. No wire change.
**Depends on:** 00-bench; 09 (shares the list of decode attempts).
**Switch:** `MERCURY_NOISE_WEIGHT` / `[audio] rx_noise_weight`, default off.

## Goal

Stop one interfered carrier (or one noisy stretch of a frame) from poisoning
a whole codeword, by scaling each soft bit with the noise actually measured
on its carrier and symbol.

## Facts

- `freedv_700.c:555`: `EsNo = pow(10, ofdm->EsNodB / 10)`; a constant per mode
  (3 dB default, 10 dB for `qam16c2`, `ofdm_mode.c:54,131`).
- `symbols_to_llrs(llr, syms, amps, EsNo, mean_amp, bps, nsyms)`
  (`mpdecode_core.c:640`). Inside `Demod2D` the metric of a symbol is
  `-EsNo * |r/mean_amp − amp·S/mean_amp|²`. **Scaling both `r` and `amp` of a
  symbol by `s` multiplies its LLRs by `s²`.** That is the whole mechanism:
  no change to the demapper is needed.
- Symbol index in the packet buffers: `k = frame*Nsymsperframe + rr*nc +
  (i-1)`, so `k % nc` is the carrier (0-based) and `k / Nsymsperframe` the
  modem frame. This holds before `ofdm_disassemble_psk_modem_packet_with_
  text_amps` removes the unique-word symbols, so apply weights **before**
  disassembly.
- Pitfall 3 (LLR scale sensitivity) is the main risk.

## Prototype result

DATAC1, AWGN +5 dB, CW carrier at 1650 Hz: at −3 dB relative to the signal
stock 0/60, weighted 60/60; at +3 dB stock 0/60, weighted 53/60; at −15 and
−9 dB both 60/60 (`prototype/results/D_tone.txt`).

## Design

### Residuals (in `ofdm_demod_core`, every modem frame, cheap)

Using the per-carrier pilot products of spec 09 (`h_prev`, `h_this`,
`h_next`):

```c
d = h_this[i] - 0.5f * (h_prev[i] + h_next[i]);   /* cancels slow channel change */
nv_carrier[i] += cnormf(d) / 1.5f;                /* 1.5 = variance of that combination, in units of one pilot's noise */
nv_frame[frame] += cnormf(d) / 1.5f;              /* same quantity summed over carriers: per-frame noise */
nv_n++;
```

Skip the burst's first modem frame (no valid previous pilot); reset all
accumulators there. New fields in `struct OFDM`: `float *nv_carrier`
(`nc + 2`), `float *nv_frame` (`np`), `int nv_n`. Fill them whenever either
this feature or telemetry (spec 10) is on.

### Weights (in `freedv_700.c`, at packet completion)

```c
med   = median over carriers of nv_carrier[c] / nv_n;
w_c   = nv_c > K * med ? med / nv_c : 1.0;        /* K = 2.0 */
scale = sqrtf(w_c);                                /* applied to symbol and amplitude */
```

Copy `rx_syms`/`rx_amps` to scratch arrays, multiply entry `k` by
`scale[k % nc]`, and hand the scratch arrays to the shared
"symbols to decoded bits" function from spec 09. Untouched carriers keep a
factor of exactly 1, so with no interference the attempt is numerically the
stock decode.

Per-frame (time) weights, phase 2: the same with `nv_frame[k /
Nsymsperframe]` against the median frame. Combine by multiplying the two
weights. This is what gives the blanker (spec 08) a soft counterpart.

### Where it sits in the decode attempts

Attempt order on a packet (stop at the first CRC pass):

1. stock (always, unchanged);
2. HARQ combine (existing, only when enabled and a copy is retained);
3. alternative channel estimate (spec 09), with noise weights applied if
   this feature is on;
4. stock estimate with noise weights (only if 3 is off or failed).

Phase 1 ships weights only inside fallback attempts, which cannot lose a
frame that decodes today. Whether weights should move into attempt 1 is a
later, measured decision.

### API and switch

```c
void freedv_set_noise_weight(struct freedv *f, int on);
void freedv_get_noise_profile(struct freedv *f, float *per_carrier, int *n);  /* for telemetry and tests */
void freedv_set_esnodb(struct freedv *f, float db);   /* bench only: calibration sweeps */
```

Backend hook `set_noise_weight`, `modem_set_rx_noise_weight(int)`, env and
ini as in the handbook.

## Steps

1. Residual accumulators and `freedv_get_noise_profile`; unit test on a
   synthetic burst with one jammed carrier that the profile peaks on that
   carrier and is flat without it.
2. Weights in the fallback attempts; switch.
3. Bench: tone sweep (level −15..+10 dB, three frequencies: on a carrier
   centre, between two carriers, at the band edge), an FSK interferer, two
   simultaneous tones; modes DATAC3, DATAC1, DATAC17, QAM16C2; AWGN and
   `ccir-poor`. Neutrality control on every preset with no interferer.
4. Per-frame weights with the impulse bench of spec 08.
5. `K` sweep (1.5, 2, 3, 4) and re-check of `EsNodB` (0, 1.5, 3, 4.5 dB) for
   weighted attempts.
6. Findings document `docs/NOISE-WEIGHT-FINDINGS.md`.

Later phases, each its own change:

7. **Gap noise profile.** Measure the noise spectrum while no burst is being
   received (the decoder is in `search` and the carousel expects silence) and
   use it to seed `nv_carrier` before the first pilot. Needs the telemetry
   plumbing of spec 10.
8. **Decode-and-subtract.** After a frame passes CRC, re-modulate it
   (`freedv_rawdatatx` on a scratch instance), align and scale it with the
   channel estimate, subtract from the stored samples, and fit steady tones
   to the residual (frequency, amplitude, phase by FFT peak interpolation).
   Subtract those tones from the next burst's samples before demodulation.
   Research: do only after phases 1-6 show narrowband interference is common
   on air.
9. **Notch for strong carriers.** A tone more than about 10 dB above the
   signal leaks into many carriers through the rectangular FFT window and
   disturbs acquisition; weights alone cannot fix that. An adaptive notch on
   the captured audio, driven by the gap profile, is the complement.

## Tests

`tests/modem/test_noise_weight.c`:

- profile test (step 1);
- `test_off_is_identical` as in spec 09;
- `test_tone_rescued`: DATAC1, AWGN +5 dB, tone at −3 dB, 30 seeded bursts:
  stock under 3 delivered, with the feature at least 27;
- `test_neutral_without_interference`: same bursts without the tone deliver
  the same count with the feature on and off.

## Acceptance

- Off: identical to trunk.
- On, no interference: delivered equals stock on every cell of the
  neutrality control (±1 frame in 100).
- On, single tone at −3 dB: at least 90 % delivered where stock delivers
  under 10 %, for DATAC1 and DATAC17.
- Noise-only false accepts: zero in 1000 windows per mode.

## Open questions

- Modes with 3-4 carriers (DATAC15/16/4) have no spare carriers: de-weighting
  one of three may not be enough. Measure; spec 16 (sub-band) exists for this
  case.
- The median as the reference fails when more than half the carriers are
  interfered. Fall back to the minimum quartile and report.
- Residuals from only `np` pilot rows are noisy for short packets (DATAC0 has
  `np = 4`); require `nv_n >= 8` before weighting.
