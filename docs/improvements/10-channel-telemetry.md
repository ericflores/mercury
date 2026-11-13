# 10 — Channel and interference telemetry (issue #10)

**Kind:** receiver only, logging. No decision uses it at first.
**Depends on:** 07 (pilot residuals), 08 (blanker statistics) for two of the
fields; the rest stands alone.
**Switch:** `MERCURY_CHANSTATS=1` / `[main] chanstats`, default off.

## Goal

Measure and log what is limiting the link, so later policies (and operators)
can tell white noise, narrowband interference, impulses, excess delay spread,
fast Doppler and transmitter distortion apart. **This proposal changes no
behaviour.**

## Fields and how to compute them

Per decoded or failed payload frame (computed in the modem, cheap):

| field | computation | where |
|---|---|---|
| `snr` | existing estimate (`freedv_snr_calib`) | have it |
| `foff` | `be->get_freq_offset` | have it |
| `nv_car_max`, `nv_car_med` (dB) | max and median of the per-carrier pilot residual (spec 07) | `ofdm.c` accumulators |
| `nv_car_idx` | carrier index of the maximum | same |
| `nv_frm_max` (dB over median) | worst modem frame's residual against the median frame | same |
| `dopp` (Hz) | Doppler spread estimate: for each carrier, correlation between consecutive pilot rows `ρ = |Σ h_t · conj(h_{t+1})| / Σ|h_t|²`, averaged over carriers; for a Gaussian spectrum `ρ = exp(−2π²σ²T²)` with `T` = modem frame period (0.11 s for the 6 ms-prefix modes), spread = 2σ | `ofdm.c`, pilot rows |
| `dly` (ms) | delay spread estimate from the decorrelation across carriers after removing the mean phase slope: `ρ_f = |Σ h_i · conj(h_{i+1})| / Σ|h_i|²`; for two equal paths `ρ_f = |cos(π Δf τ)|` with `Δf` = carrier spacing (62.5 Hz) | `ofdm.c` |
| `evm` (dB) | error-vector magnitude of the data symbols against the nearest constellation point after equalisation, relative to symbol power | `freedv_700.c`, `ofdm_esno_est_dd` is close to this already |
| `evm_excess` (dB) | `evm` minus the EVM predicted from the pilot-residual noise alone; large and positive means self-interference (ISI, Doppler, transmitter distortion) | derived |
| `iters`, `pcc` | LDPC iterations used and parity checks satisfied (already available as `iter`, `parityCheckCount` in `freedv_700.c`) | have it |
| `path` | which decode attempt delivered (spec 09) | have it |

Per silent gap (the payload decoder is in `search` and the carousel does not
expect a transmission: guards, between-burst gaps after a failed burst, the
wait for a poll):

| field | computation |
|---|---|
| `gap_floor` (dBFS), `gap_flat` | median bin power of a 256-point FFT average over the gap; spectral flatness (geometric / arithmetic mean) |
| `gap_peaks` | up to 3 bins more than 10 dB over the median: frequency and level |
| `gap_kurt` | kurtosis of the samples (3 = Gaussian); peak/rms |
| `gap_per` | strongest envelope periodicity between 90 and 130 Hz (autocorrelation of |x|²) |
| `blank` | blanker's blanked fraction over the last 10 s (spec 08) |

The busy detector (`modem/channel_busy.c`) already runs an FFT on captured
audio; reuse its spectrum where possible instead of adding a second FFT.

Per round (carousel, already known): frames asked, frames delivered, the
positions lost.

## Output

- One log line per frame at DEBUG and one per round at INFO, compact
  `key=value`, with a fixed prefix (`chanstats:`) so `utils/onair_logs.py`
  can parse it. Extend that script to summarise a session: share of frames by
  limiting factor.
- Websocket status: add the per-round summary to the status snapshot
  (`gui_interface/ui_status.{c,h}`), so the UI can show "limited by:
  interference / fading / distortion / noise" later.

## First classification (reported only, no policy)

Order matters; first match wins:

1. `blank` > 0.5 % or `gap_kurt` > 6 → impulsive.
2. `gap_peaks` present, or `nv_car_max − nv_car_med` > 6 dB → narrowband
   interference.
3. `evm_excess` > 3 dB and `dly` > half the mode's cyclic prefix → delay
   spread (ISI).
4. `evm_excess` > 3 dB and `dopp` > 2 Hz → Doppler.
5. `evm_excess` > 3 dB, flat channel (`dly` < 0.5 ms), SNR > 12 dB →
   transmitter distortion at the peer.
6. `snr` within 3 dB of the rung's threshold → noise-limited.
7. otherwise → not limited (clean).

Thresholds are starting points; calibrate them on the presets.

## Steps

1. Estimators for `dopp` and `dly`; validate against every Watterson preset
   (true values are in the table in `common/watterson.c:445-458`). Report
   bias and spread in the findings; expect them to be coarse (a single 3-7 s
   burst cannot resolve 0.1 Hz).
2. `evm` and `evm_excess`; validate that it is near 0 dB on AWGN at every SNR
   and rises on `itur-mdv` at high SNR and with a clipped transmit signal.
3. Gap statistics with a clean interface to "is this a silent gap"
   (the carousel knows; the modem can approximate with "payload decoder in
   search for more than 100 ms and energy within 3 dB of the tracked floor").
4. Log lines, `onair_logs.py` summary, status fields.
5. Confusion matrix on synthetic cases using `rx_bench` impairments:
   AWGN, tone, impulses, `itur-mdv`, `ccir-flutter`, clipped transmitter.
6. Findings document `docs/CHANSTATS-FINDINGS.md`. Collect on-air logs.

Only after on-air data: the first policy candidate (its own proposal) is a
ladder cap on ISI-limited links, the case
`docs/FADE-CLIFF-DECISION.md` identifies ("delivery persistently
contradicting SNR should cap the ladder"). Judge it on the NVIS sim channels
(`nvis`, `nvisa:`) with 60-100 seeds like every ladder change.

## Tests

- Unit tests for each estimator on synthetic pilot sequences with known
  Doppler and delay (deterministic, no modem needed).
- `test_chanstats_off_is_silent`: no extra log lines and no measurable CPU
  when off.

## Acceptance

- Off: no behaviour or log change.
- `dly` separates 0.5 ms from 2 ms from 7 ms presets; `dopp` separates
  0.1-1 Hz from 10 Hz presets.
- Synthetic confusion matrix: each of the six conditions classified
  correctly in at least 80 % of frames at SNR 10 dB above the rung threshold.
