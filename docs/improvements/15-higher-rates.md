# 15 — Higher code rates, 64QAM, wider carrier sets (issue #15)

**Kind:** wire change (new rungs). **Depends on:** 09 (channel estimate
quality), 02 (clipping), the capability scheme.
**Switch:** each new rung is offered only if `[arq] max_rung_ext` (or
`MERCURY_EXT_RUNGS=1`) allows it and the peer announced it.

## Goal

Raise the top speed in measured steps, cheapest first. Each step is kept
only if its goodput beats the rung below somewhere realistic.

## Facts

- Ladder: `LADDER[]` in `carousel.c:182` (7 rungs, `CAR_NLEVELS`), thresholds
  `ARQ_SNR_MIN_*` in `arq_protocol.h:314-324`, mode timing table in
  `arq_protocol.c`, SNR calibration per mode in `freedv_snr_calib()`
  (`freedv_700.c:497`), start rung from SNR in `car_start_level()`.
- QAM16C2 (`ofdm_mode.c:104-135`): `ns` 5, `np` 31, `tcp` 4 ms, `ts` 16 ms,
  `nc` 33, `bps` 4, `nuwbits` 168, code `H_16200_9720`, `EsNodB` 10, no
  clipping. DATAC17 (137-169): same code, QPSK, `np` 61, `tcp` 6 ms,
  clip 2.7/0.8.
- LDPC codes are registered in `modem/freedv/ldpc_codes.c` (a table of
  name, max iterations, sizes, `H_rows`, `H_cols`) with one `.c/.h` pair per
  code (`H_16200_9720.{c,h}`); the build list is `OBJS_COMMON` in
  `modem/freedv/Makefile`. The decoder is generic.
- The rung is a 3-bit field on the wire (POLL byte 1 and 11, ACCEPT byte 8,
  DATA byte 0): values 0-7, value 7 unused. `decode_ctl` rejects
  `level >= CAR_NLEVELS`.
- Frame sizes must be pairwise distinct across modes.
- 16QAM soft demapping exists (`S_matrix_qam16`, `symbols_to_llrs` asserts
  `bps == 2 || bps == 4`); 64QAM does not.

## Step 1: rates 3/4 and 5/6 on the 16QAM waveform

1. **Codes.** `H_16200_9720` has the dimensions of the DVB-S2 short frame at
   nominal rate 3/5. Confirm its origin (file header, upstream codec2
   history) and generate the same family's other short-frame codes from the
   standard's published tables (ETSI EN 302 307, Annex C): nominal 3/4
   (K = 11880) and 5/6 (K = 13320). Write a small generator script under
   `modem/freedv/tools/` that emits the `H_rows`/`H_cols` arrays in the
   format of `H_16200_9720.c`, and a unit test that encodes and decodes
   random payloads with each.
   If the existing code turns out not to be from that family, any published
   rate-compatible LDPC family of about this length will do; the rest of
   this step is unchanged.
2. **Modes.** New entries in `ofdm_mode.c` copying `qam16c2` with the new
   `codename` and an `np` that makes the frame size unique. Payload bytes =
   K/8 − 2 adjusted for the unique word and for shortening; choose `np` and
   the shortening so the payload differs from every existing mode's.
   Mode numbers continue the Mercury range used by DATAC15-17/QAM16C2 (see
   `freedv_api.h`). Add to `pool_modes[]`, the timing table (measured
   duration), `bench_mode_from_name`, `mercury -l`.
3. **Bench.** `acquire_vs_decode` and `qam16_papr_sweep` (equal-peak model,
   clipped as in spec 02): delivery curves on AWGN, `ccir-moderate`,
   `ccir-poor`. Then goodput crossover against QAM16C2 exactly as
   `docs/CLIPPED-QAM16C2-FINDINGS.md` computes it against DATAC17.
4. **SNR calibration** map for the new mode (`utils/snr_calib_sweep.sh`,
   `docs/OTA-PHASE-A-SNR-CALIB.md` describes why it is needed) and its
   `ARQ_SNR_MIN_*`.
5. **Ladder.** The better of the two becomes rung 7 (the one free value).
   `CAR_NLEVELS` 8; audit every array sized by it and every `>= CAR_NLEVELS`
   check; a peer without the capability must never be asked for rung 7 and
   never names it (`cap_level`, `car_set_max_level`, `session_max_level()`).
6. **Sim.** The sim's per-mode cliff model (`sim_channel.c`) gets the new
   rung's measured curve; run the ladder matrices (clean, `awgn:*`,
   `cliff:*`, `fade:*`, `step:*`), 60-100 seeds.

Expected (from the published performance of that code family, to be
confirmed on this waveform): rate 3/4 about 3900 bit/s at about +2.5 dB over
QAM16C2's requirement; rate 5/6 about 4300 bit/s at about +4 dB.

## Step 2: fill the gap between DATAC17 (+7 dB) and QAM16C2 (+13 dB)

Candidates: QPSK at rate 3/4 (about 1750 bit/s), 16QAM at the family's
nominal rate 1/2 (about 2300 bit/s). Same procedure. Needs more than one new
rung value, so the 3-bit rung fields must be widened or re-mapped per
capability: a protocol revision to design with the maintainers. Do not start
before step 1 has shown the procedure works.

## Step 3: 64QAM

- Constellation, mapper and `S_matrix_qam64` in `mpdecode_core.c` /
  `ofdm.c` (`qam16[]`, `qam16_demod`, the `bps == 4` branches: grep for
  `bps == 4` and `bps == 2`).
- Decision-directed SNR estimate (`ofdm_esno_est_dd`) for 64 points.
- Requires spec 09's estimator as the **primary** path for this mode (a
  2-pilot reference is far too noisy for 64QAM) and probably amplitude
  interpolation between pilot rows.
- PAPR work as in spec 02.
- Realistic only for transmitters that are linear with ALC off; state that
  in the mode's documentation.

## Step 4: wider carrier sets

`nc` 37 (2.31 kHz) for BW2300 sessions and 43 (2.69 kHz) for BW2750, with new
TX band-pass prototypes (`filtP1100S1300` is the current one; see
`modem/freedv/filter_coef.h` or wherever the `filtP*` tables live) and
`rx_centre`/`tx_centre` unchanged. The session bandwidth already caps the
ladder (`session_max_level`); wide rungs are offered only in sessions whose
both ends configured that bandwidth.

## Tests

- Round-trip tests for each new code and mode; frame-size uniqueness test
  over the whole mode table (add one if none exists).
- Mixed-version: rung 7 never appears on the wire toward a peer without the
  capability; a frame naming rung 7 from such a peer is still rejected.
- Ladder: on the sim's clean channel the new rung is reached and held; on
  `cliff:` channels below its threshold it is probed at most as often as
  QAM16C2 is today.

## Acceptance (per new rung)

- A goodput crossover exists: there is an SNR range on AWGN and on at least
  one fading preset where the new rung beats the one below at equal
  transmitter peak. If not, do not add it.
- Sim matrices: clean and high-SNR channels faster; no channel more than 2 %
  slower.
- OTA: reached and held on at least one real path before it is enabled by
  default.

## Risks

- A 3 s frame at rate 5/6 has almost no protection against a fade inside
  it; expect the rung to be useful only on steady, strong paths.
- Real transmitters (ALC, audio chain, IMD) may cap usable SNR below what
  these rungs need. The peer-distortion indicator of spec 10 is the tool to
  see that on air.
