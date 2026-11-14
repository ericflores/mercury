# 27 — A decision model that names what is degrading the signal and steers the receiver (issue #27, research)

**Kind:** research first; if it wins, receiver only, no wire change.
**Depends on:** 10 (the features and the rule-based classification it must
beat), 00-bench (impairment library, replay of recordings), 23 (the attempt
list it orders). Its usefulness grows with every action that exists: 07 and
08 are not implemented yet (see "What it can steer").
**Hardware:** a GPU for training and fine-tuning (a GTX 1080 Ti is enough).
None required for use; an accelerator is an optional backend.
**Switch:** `MERCURY_RX_CLASSIFIER=off|rules|model` /
`[audio] rx_classifier`, default `off`.

The idea is the fork owner's: use the compute a desktop with a GPU has to
recognise the mode of degradation, with a decision model fine-tuned for it,
from sequential samples of the signal in time and frequency, as an optional
module for stations that have the hardware.

## Goal

A small model that, from what the receiver already sees, answers a fixed set
of typed questions about the channel ("what is limiting this link, and how
sure are you"), and a policy layer that uses the answers to pick the
receiver's tools. Find out with numbers whether a trained model does this
better than proposal 10's threshold rules, and whether steering the receiver
by it buys frames, time or both. Integrate only what wins.

This is **not** proposal 22. Proposal 22 is a network that produces soft
bits, in the data path, judged in dB against the genie. This one produces
decisions, outside the data path, judged by whether the decisions are right
and whether acting on them helps. It is far smaller, and a wrong answer
costs a wasted attempt, never a wrong byte.

## Facts

- The receiver runs a fixed list of decode attempts on a failed frame:
  stock, HARQ combine, 09, 19, 26 (`freedv_comp_short_rx_ofdm`,
  `modem/freedv/freedv_700.c`; `freedv_get_rx_decode_path()` returns which
  one delivered). The order is fixed and each attempt that fails is a full
  LDPC run: about 60 ms for DATAC3, 100 ms for DATAC1, 210 ms for DATAC17,
  235 ms for QAM16C2 on one 2.1 GHz Xeon core (proposal 23). A Raspberry Pi
  has not been timed.
- Which attempt helps depends on the channel, and the bench already shows
  it: on disturbed NVIS only 26 delivers and 09 and 19 add nothing
  (`docs/PROFILE-TRACKING-FINDINGS.md`); on white noise and CCIR Poor 09 and
  19 deliver and 26 adds nothing to them. A receiver that knew which channel
  it is on would skip the attempts that cannot work.
- Proposal 10 defines the measurements (per-carrier pilot residual, Doppler
  and delay spread estimates, EVM excess, and for silent gaps the noise
  floor, spectral peaks, kurtosis and envelope periodicity) and a first
  classification by thresholds into seven conditions: impulsive, narrowband
  interference, delay spread, Doppler, peer distortion, noise-limited,
  clean. It is not implemented.
- Proposal 26 already measures the delay profile on every frame it replays
  (`ofdm_profile_paths()`, `modem/freedv/ofdm.c`): lags and powers of up to
  three paths.
- The busy detector (`modem/channel_busy.c`) runs an FFT on captured audio
  while the station listens.
- `MERCURY_RX_DUMP=<file>` (`modem/modem.c`, `rx_dump_chunk`) writes every
  chunk the decoders are handed, s16le at 8 kHz: the recording tool exists.
- Decoding runs in per-plane worker threads (`rx_worker_thread`,
  `modem/modem.c`); anything this proposal runs must fit beside them inside
  `D_OFDM_MS` (1000 ms, `datalink_arq/carousel.c`).
- Speed is not the constraint for inference: a DATAC1 frame is 27 carriers
  by 190 symbols every 4.4 s (research note, section 5). The GPU is for
  training.

## What a decision model is, and why the shape fits

"Decision models" (the first announced was TypeSafe AI's Jev, September
2026, under the name "System One model") take a state and a set of typed
questions and return typed answers with calibrated probabilities: a choice
from a list, a score, a yes or no. They produce no text. Reported here from
third-party coverage only; the announcement itself could not be read from
the session that wrote this (see "Sources").

That contract is the right one for this job, whatever model sits behind it:

- **Typed answers.** The modem cannot act on prose. It can act on
  `limit = delay_spread, p = 0.91`.
- **Calibrated probabilities.** The policy needs to know when the model does
  not know. An answer at `p = 0.4` must leave the receiver on its default
  order.
- **A fixed question set**, versioned, so the C code and the model agree.

What does **not** fit is a hosted model. As reported, Jev is closed, reached
through a web API, with no weights to run locally. A modem that exists for
links with no internet cannot call an API per frame, and a station's
received audio should not leave the station. So:

- The model that ships in `mercury` is trained by this project and runs
  locally from a weights file.
- A hosted or third-party decision model can be a **research arm only**:
  given the same features offline, does it classify better than the small
  local model? If one with open weights and a compatible licence exists and
  wins, it becomes a candidate backend. Check availability and licence at
  the time; nothing here has verified any of them.

## The interface (fixed first, before any model)

One C header, `modem/rx_classifier.h`. Everything behind it is replaceable.

**State in** (what the receiver already has; no new signal processing beyond
proposal 10):

- per frame: the fields of proposal 10 (`snr`, `foff`, `nv_car_*`,
  `nv_frm_max`, `dopp`, `dly`, `evm`, `evm_excess`, `iters`, `pcc`, `path`),
  the mode, whether it decoded; with 26 on, the path lags and powers;
- per silent gap: proposal 10's gap fields, and a small spectrogram of the
  gap (for example 64 bins by 32 time steps of log power, 0-4 kHz) and the
  sample kurtosis per time step: the time and frequency view of the
  interference;
- a sequence: the last N frames and gaps of the session (start with N = 16),
  so the model sees how the channel moves, not one snapshot.

**Typed answers out**, each with a probability:

| question | type | values |
|---|---|---|
| `limit` | choice | `clean`, `noise`, `impulsive`, `narrowband`, `cochannel`, `delay_spread`, `doppler`, `peer_distortion` |
| `interferer` | choice | `none`, `carrier`, `fsk`, `voice`, `data_modem`, `sweeper`, `powerline`, `inverter`, `static` |
| `impulse_period_hz` | number or none | 90-130 Hz and harmonics, for the blanker |
| `interferer_band_hz` | range or none | where the narrowband interferer sits |
| `will_hold` | yes/no | will the present rung still deliver in the next round |

`limit` is proposal 10's classification plus `cochannel`. `interferer` is
where a trained model should earn its place: telling a carrier from RTTY
from another data modem from an ionosonde sweep in a spectrogram is pattern
recognition that thresholds do badly. `will_hold` is the sequential question
(see "Phase 4").

**Backends**, chosen by the switch:

- `rules`: proposal 10's thresholds, extended to the questions above where a
  rule exists, `none` elsewhere. This is the baseline and the fallback.
- `model`: the trained model, plain C inference from a weights file
  (`[audio] rx_classifier_model = <path>`). If the file is missing or its
  version does not match, fall back to `rules` and log once.

## What it can steer

The answers are worth what the receiver can do with them. Today:

| answer | action | exists? |
|---|---|---|
| `delay_spread` | try 26 first; skip 09 and 19 on a failed frame | yes |
| `noise` | try 09 then 19; skip 26 | yes |
| `impulsive`, period | enable the blanker with that period (08) | no: 08 is a measured prototype |
| `narrowband`, band | per-carrier weights (07); later move the sub-band (16) | no: 07 is a measured prototype, 16 is not measured |
| `cochannel` | hold off (busy detector); later QSY (24) | busy detector yes, 24 design only |
| `peer_distortion` | report it (10); telling the peer needs a wire field that is not designed | no |
| `doppler` | nothing yet (20 is the candidate) | no |

So before 07 and 08 land, the only policy that can be tested is the order of
attempts 09, 19 and 26. That is still a real test (it is the CPU saving a
slow machine wants), and it is Phase 3's first step. Do not start Phase 3
expecting more than that until 07 and 08 exist.

Policy rules that do not change with the model:

- The classifier never touches the data path. It chooses which existing
  attempts run and in what order, and which existing switches are on. The
  LDPC decoder, the parity gate and the CRC decide what is delivered.
- Below a probability threshold (start at 0.8, tune on the bench) the
  receiver uses its default order. A skipped attempt is only skipped
  **first**: with effort to spare (proposal 23) it still runs last, so the
  classifier can cost time on a wrong answer but not a frame.
- It does not move the ladder. Any use of `will_hold` or `limit` by the
  carousel is a separate proposal judged on the simulator with 60-100 seeds,
  like every ladder change. The constants in `carousel.c` stay as they are.

## Order of work

### Phase 0: data, and the rule baseline (no model, no GPU)

1. Implement proposal 10 (it is this proposal's prerequisite and its
   baseline). Its findings give the confusion matrix of the rules.
2. A dataset tool, `utils/classifier_dataset.c`, built on `rx_bench`: for
   each trial write the state above and the label (the preset and the
   impairment that were applied). Document the layout in its header.
3. Recordings with `MERCURY_RX_DUMP` from the equipment on hand
   (handbook, "Test equipment on hand"): the IC-7300 and the SDRplay RSP1B,
   noise only, on quiet frequencies and on occupied ones, near inverters and
   power lines, during static; a note per recording of what was there (the
   label). Synthetic Mercury bursts are added at chosen levels, as in
   proposal 22: real interference, exact knowledge of the wanted signal.
   One operator can do all of it without a second station.
4. Keep whole recordings (and whole days) out of training, for testing.

**Stop here if** the rules already classify the recordings well enough that
the actions that exist would be chosen correctly: a model would then add
nothing but weight.

### Phase 1: the model, offline (GPU)

1. Train a small model on the Phase 0 data. A starting point, not a
   prescription: a few 2-D convolutions over the gap spectrogram, a small
   recurrent or attention layer over the sequence of N states, one output
   head per question. Tens to a few hundred thousand parameters. Loss:
   cross-entropy per head; then calibrate (temperature scaling on held-out
   data) and report calibration, not only accuracy.
2. Compare with `rules` on the same held-out set: per-question accuracy,
   confusion matrices, calibration curves, by SNR.
3. The research arm: the same held-out states to any external decision
   model that is available and whose terms allow it. Recordings stay local
   unless the owner decides otherwise; a synthetic-only subset can be used
   for a hosted model.
4. Findings, whatever the outcome.

**Stop here if** the model does not beat `rules` on real recordings by a
margin that changes which action is chosen.

### Phase 2: in the modem, reporting only

1. `rx_classifier.h`, the `rules` backend, the `model` backend as plain C
   (no framework, no new dependency; export weights to a flat file with a
   version and a hash of the question set).
2. Log line `classifier:` per round with the answers and probabilities, next
   to proposal 10's `chanstats:`; status field for the UI.
3. Measure the cost per frame on x86 and on a Raspberry Pi 4. Budget: it
   must run inside the gap between bursts and take a small fraction of one
   failed decode; if it does not on a Pi, the Pi uses `rules`.
4. No behaviour changes in this phase. A test proves it.

### Phase 3: steering

1. Order of the attempts 09, 19, 26 by `limit`, under proposal 23's budget.
   Measure on `rx_bench` over a mix of presets: frames delivered (must be
   identical to all-attempts-on with effort to spare, and no fewer than the
   fixed order under a tight budget) and CPU time on failed frames.
2. As 07 and 08 land: the blanker's enable and period, the carrier weights'
   band.
3. Integration A/B (`tests/integration`, the script behind
   `docs/INTEGRATION-AB-FINDINGS.md` once it exists) on sessions.

### Phase 4: looking ahead (the sequential question)

`will_hold`: from the sequence of states, will this rung still deliver next
round. Labels come free from sessions (what happened next round). Evaluate
offline first: does it predict better than "same as last round" and better
than the ladder's own evidence? Only if it does is a ladder proposal worth
writing. Expect this to be the hardest part to show, and the one most
exposed to the simulator not being the air.

### Optional: an accelerator

Inference of a model this size does not need one. If a later, larger model
does, add a backend behind the same interface that is compiled only when
asked for (`make HAVE_CLASSIFIER_GPU=1`) and selected only when the runtime
finds the device and its driver; otherwise the CPU backend or `rules` is
used. The default build gains no dependency. Measure before building this:
a GPU round trip for a model of this size may well be slower than the CPU.

## Training data: what makes or breaks it

A model trained only on the project's channel model learns the channel
model (research note, section 5). The interferer classes in particular
cannot come from a simulator: they have to be recorded. The order of trust:

1. real recordings with notes, the test set always drawn from these;
2. real noise plus synthetic bursts;
3. synthetic channels and synthetic impairments (00-bench), for breadth.

Labels from one operator's notes are coarse and sometimes wrong. Record the
uncertainty; prefer fewer classes that can be labelled reliably to many that
cannot. Merge classes that the recordings cannot separate.

## Tests

- `test_classifier_off_is_silent`: off, no log line, no CPU, identical
  decoded bytes.
- `rules` backend: unit tests on synthetic states for each rule.
- `model` backend: the C inference reproduces the training framework's
  outputs on a stored set of states to within float tolerance; a truncated
  or mismatched weights file falls back to `rules`.
- Steering: with effort to spare, the delivered set equals the fixed
  order's on the same samples (`rx_bench`, 200 trials a point).

## Acceptance

- Off: no behaviour or log change.
- Phase 1: on held-out **real** recordings the model beats `rules` on
  `limit` and `interferer`, and its probabilities are calibrated (stated
  0.8 means right about 80 % of the time, within the test set's error).
- Phase 3: no point of any bench table shows fewer frames delivered than
  the fixed order with the same budget; CPU time on failed frames falls on
  at least the NVIS and white-noise presets.
- Costs measured on a Pi 4 and stated.

## Risks

- **It may not beat the rules.** Seven conditions with good features is a
  problem thresholds often solve. The interferer types and the sequential
  question are where a model has room; the rest may be a draw. That is an
  acceptable result: Phase 0 and the `rules` backend are useful by
  themselves.
- **Simulator to air.** The main risk, as for proposal 22. Mitigated only by
  recordings.
- **Too few actions.** Until 07, 08, 16 and 24 exist the model names things
  the receiver cannot act on. Phase 3 is small until then.
- **Opacity for the maintainers.** A weights file is harder to review than
  a threshold. Hence the fixed typed interface, the `rules` backend as the
  default of the two, the calibration requirement, and reporting-only before
  steering.
- **One operator's recordings** cover one location, one antenna and a few
  months. Say so in the findings; ask other stations for dumps.

## Sources

Decision models and Jev, third-party coverage only (the vendor's page was
not reachable from the session that wrote this, so nothing below was checked
against it; verify before relying on any of it):

- <https://www.theneuron.ai/explainer-articles/typesafe-jev-system-one-models-explained/>
- <https://letsdatascience.com/news/typesafe-ai-launches-jev-decision-model-889a38c0>
- <https://docs.typesafe.ai/concepts/system-one>
- the announcement the owner pointed to:
  <https://typesafe.ai/blog/introducing-system-one-models-and-jev>
