# What more compute could buy: desktop CPU, GPU and neural networks

Research note, 2026-11-13. It answers two questions from the fork's owner:

1. Can a machine with far more compute than a Raspberry Pi (for example a
   desktop with a GTX 1080 Ti) decode what a Pi cannot, by running heavier
   error correction or more resilient algorithms?
2. Are there deep-network models that improve reception in bad conditions or
   remove noise, and could they be used here?

Everything marked *measured* was run in this repository on 2026-11-13 with
the scratch code kept in `prototype/lab2/`. Everything else is literature or
arithmetic and says so.

## Short answer

- **Yes, there are several dB left, and most of them do not need a GPU.**
  After proposal 09, an ideal receiver of the *same on-air signal* would
  still work 2.4 to 6 dB lower, depending on the mode (table in section 1).
- **The first of those dB are cheap.** Reusing the cyclic prefix that the
  receiver throws away is worth another 0.5-0.8 dB on top of proposal 09
  (*measured*), at almost no CPU cost.
- **The next 1-2 dB need a receiver that iterates** between decoding and
  channel estimation. A "genie" that knows the transmitted symbols shows the
  ceiling: about 1.0-1.35 dB more on a noise-limited channel, about 2 dB more
  under CCIR Poor, and two to three times the frames on disturbed NVIS
  (*measured*). This is what a decision-directed or a neural receiver
  competes for.
- **Brute force on the decoder does not pay.** Sixteen times the LDPC
  iterations gives about 0.1 dB; fifty perturbed decodes give nothing; the
  pieces inside a failed frame are almost never intact (*all measured*). The
  information is lost before the decoder, in estimation and acquisition.
- **Where raw compute matters is latency.** A decode that fails costs about
  0.2 s of a server core for the largest code, and the protocol allows about
  1 s. A Pi has room for one attempt, a desktop for several, a GPU for
  thousands. So the useful design is a receiver whose number of attempts
  scales with the machine.
- **Neural networks: yes, with a clear boundary.** Learned receivers are real
  and gain 1-3 dB over practical classical receivers in the literature, most
  of all with sparse pilots, fast fading and non-Gaussian noise. They cannot
  predict payload bits (compressed data has nothing to predict); they learn
  the channel and the noise. Their gain here is bounded by the genie numbers
  above. At HF data rates inference is trivial even for large models; the GPU
  is needed for *training*, and the hard part is training data that looks
  like real HF. Recorded real noise plus synthetic Mercury bursts solves
  most of that (section 5).

## 1. How far the receiver is from the limit

Signal-to-noise ratio in 3 kHz needed for about 90 % delivery on white noise.
"Ideal receiver" is the finite-length bound (normal approximation, 10 % frame
error) for the mode's own modulation, code length and rate, with perfect
channel knowledge; "+CP" also uses the energy in the cyclic prefix. Computed
by `prototype/lab2/gap.py`.

| mode | stock (measured) | with 09 (measured) | ideal receiver | ideal receiver +CP | Shannon, same rate and bandwidth |
|---|---|---|---|---|---|
| DATAC16 (control) | −11.0 ¹ | −11.6 ¹ | −16.3 | −17.7 | −19.8 |
| DATAC15 | −10.0 ¹ | −10.6 ¹ | −13.8 | −15.2 | −17.5 |
| DATAC3 | −3.2 | −4.1 | −6.8 | −8.2 | −10.4 |
| DATAC1 | +1.1 | 0.0 | −2.2 | −3.6 | −5.6 |
| DATAC17 | +3.7 | +1.9 | −0.1 | −1.5 | −3.8 |
| QAM16C2 | +8.6 | +6.9 | +5.5 | +4.5 | +1.1 |

¹ of the bursts that were acquired; these modes lose most frames to
acquisition below about −11 dB.

Reading it:

- Between "with 09" and "ideal receiver +CP" lie 2.4 dB (QAM16C2) to 6.1 dB
  (DATAC16). That is the most any receiver-only change can still recover. It
  is a ceiling: part of it belongs to the particular LDPC codes, which are
  not as good as the bound (the short control code least of all).
- Between "+CP" and Shannon lie 2-3.4 dB that only a different waveform can
  recover (pilots, unique word, the constellation): proposal 15 and beyond.
- The control frame is the furthest from its limit. It is also the frame
  the session depends on.

## 2. What the receiver costs today

*Measured*, one 2.1 GHz Xeon core, difference between a burst whose decode
fails (all 100 iterations) and one that converges at once:

| mode | code | a failed decode |
|---|---|---|
| DATAC3 | (2048, 1024) | about 60 ms |
| DATAC1 | (8192, 4096) | about 100 ms |
| DATAC17 | (16200, 9720) shortened | about 210 ms |
| QAM16C2 | (16200, 9720) | about 235 ms |

The protocol allows about 1 s between the end of a keydown and the moment
the frame must be decoded (`D_OFDM_MS` 1000, `carousel.c`). So:

- on this core, four to nine failed attempts fit for any mode;
- a Raspberry Pi 4 core is several times slower (not measured here; the only
  in-tree figure is 0.16-0.4 s for an MFSK decode). **Proposal 09's second
  attempt doubles the cost of a failed frame, and on a Pi the two largest
  codes may not fit in the allowance.** This must be timed before 09 is
  enabled by default (proposal 23);
- published GPU decoders run belief propagation at hundreds of megabits per
  second at 10 iterations on a single card ([Rice
  University](https://typeset.io/pdf/high-throughput-low-latency-ldpc-decoding-on-gpu-for-sdr-2ll5haq3z4.pdf),
  [survey of results](https://www.mdpi.com/2079-9292/11/21/3447)). Mercury's
  fastest mode produces about 4400 coded bits per second, so a GPU could run
  on the order of 10^4 full decodes per frame. The question is what to do
  with them, and section 3 shows that "decode again" is the wrong answer.

## 3. Experiments

All with `prototype/lab2/lab2.c`: every variant decodes the same samples;
cells are delivered frames; one seed.

### 3.1 Recycling the cyclic prefix: works (proposal 19)

A sample in the cyclic prefix and the sample one symbol length later carry
the same signal and independent noise. Averaging each such pair before the
FFT lowers the noise without touching the carriers' orthogonality. The
receiver discards those samples today.

DATAC1, white noise, per 100:

| SNR3k | stock | 09 (t4f5) | 09 + recycling |
|---|---|---|---|
| −1.5 | 0 | 0 | 54 |
| −1.0 | 0 | 11 | 99 |
| −0.5 | 0 | 88 | 100 |
| 0.0 | 0 | 98 | 96 |

QAM16C2, white noise, per 60: at +6.5 dB 0 / 0 / 54; at +7.0 dB 0 / 58 / 60.

DATAC1, CCIR Poor, per 100 (stock / 09 / 09 + recycling with a 2.5 ms guard):
at +3.9 dB 68 / 87 / 94; at +1.9 dB 13 / 44 / 59.

DATAC15, white noise, per 150, time-only estimate: at −12 dB 3 / 11 / 27 of
75 acquired; at −11 dB 71 / 96 / 107 of 126. DATAC16: at −13 dB 9 / 14 / 22
of 40; at −12 dB 66 / 72 / 78 of 87.

So about 0.5-0.8 dB on top of proposal 09, on every mode tried, and it holds
under fading. The row at 0.0 dB (96 against 98) shows it can also lose a
frame now and then, so it belongs in the list of fallback attempts, not in
the first decode.

### 3.2 The ceiling for better estimation: a genie (proposals 20, 22)

The genie is told every transmitted symbol of the modem frame being
demodulated, as a receiver would know them after decoding the packet, and
estimates the channel from all of them instead of from the pilots alone. No
real receiver has this; an iterative or learned one approaches it.

DATAC1, white noise, per 60, both with recycling:

| SNR3k | best so far (09 + recycling) | genie |
|---|---|---|
| −3.0 | 0 | 1-16 |
| −2.5 | 0 | 41-54 |
| −2.0 | 0 | 56 |
| −1.5 | 33 | 59 |

DATAC1, CCIR Poor, per 60 (acquired 57, 54, 42): at +1.8 dB 36 → 51-53; at
−0.2 dB 5 → 29-33; at −2.2 dB 0 → 4-6.

DATAC1, disturbed NVIS (7 ms, 1 Hz), per 60 (59 acquired): at +9.8 dB stock
15, proposal 09 15, genie 35-36; at +7.8 dB 8, 8, 26-29.

QAM16C2, white noise, per 40: at +5.5 dB 0 → 35; at +6.0 dB 0 → 40.

Reading it:

- White noise: about 1.0-1.35 dB beyond what is implemented and measured.
- CCIR Poor: about 2 dB.
- NVIS: proposal 09 did nothing there, but knowing the data more than
  doubles delivery. The rest of the loss on NVIS is interference between
  symbols (the delay spread exceeds the cyclic prefix), which no channel
  estimate removes.
- The range of each genie cell is two variants (with and without the pilots
  of the neighbouring frames). Averaging across neighbouring frames was
  *worse*, which points at the frequency tracking loop adding phase noise
  from frame to frame at low SNR. Worth its own look.

DATAC1's 90 % points on white noise, in order: stock +1.1 dB; with 09 −0.4;
with recycling −1.15; genie about −2.1 to −2.5; ideal bound −3.55.

### 3.3 More decoder effort: nothing (do not pursue)

DATAC1, white noise, per 80:

| SNR3k | stock (100 iterations) | 400 | 1600 | 50 perturbed decodes |
|---|---|---|---|---|
| +0.75 | 22 | 31 | 36 | 22 |
| +1.0 | 70 | 76 | 77 | 70 |

Sixteen times the iterations moves the cliff by about 0.1 dB. An ensemble
of 50 decodes, each with the soft bits perturbed by 30 % of their mean
magnitude, recovers no frame (also 14 → 14 of 78 on DATAC3 at −4.5 dB).
The frames that fail are not near-misses of the decoder.

(An earlier run in this investigation appeared to show a large gain from 400
iterations. It was a bug in the scratch harness: the variant name `it400`
also matched the token `t4` and silently switched on proposal 09's
estimator. The harness now matches exact tokens; some raw result files still
carry the misleading label, see `prototype/lab2/README.md`.)

### 3.4 Salvaging pieces from failed frames: nothing (do not pursue)

If a frame fails its CRC, are some of its 24-byte pieces intact, so that the
erasure code's block check could pick them out? *Measured* on the decoder's
output for frames that failed:

| case | failed frames | residual bit error rate | pieces entirely right |
|---|---|---|---|
| DATAC1, white noise, +0.5 dB | 96 | 6.8 % | 50 of 2016 (2.5 %) |
| DATAC1, CCIR Poor, +3.9 dB | 26 | 8.9 % | 17 of 546 (3.1 %) |
| DATAC1, CCIR Poor, +1.9 dB | 76 | 13.8 % | 43 of 1596 (2.7 %) |
| QAM16C2, white noise, +8 dB | 21 | 8.9 % | 0 of 1050 |

A failed LDPC decode leaves errors everywhere. Sub-piece checksums or
error-correcting use of the outer code would recover almost nothing. What
does carry information is the frame's *soft* values, which is proposal 04
(combining repeats) and proposal 05 (decoding again with known bits).

## 4. Where compute turns into dB

Ordered by how sure the gain is. "Class" is the smallest machine that could
run it in real time: P = Pi 4, D = desktop CPU, G = GPU.

| technique | attacks | expected gain | class | wire change | basis |
|---|---|---|---|---|---|
| Second attempt, estimate over more pilots (09) | reference noise | about 1 dB | P for small codes, D for the 16200-bit codes until timed | no | measured |
| Cyclic-prefix recycling (19) | discarded energy | 0.5-0.8 dB | P | no | measured |
| Decision-directed (iterative) estimation (20) | reference noise, fast fading | up to 1.0-1.35 dB white noise, about 2 dB CCIR Poor, 2-3× frames on NVIS | D | no | genie ceiling measured; real schemes get part of it. Turbo receivers for HF serial-tone waveforms report 2 dB and up to 6-7 dB over one-pass receivers ([Pretoria](https://repository.up.ac.za/items/3dd09fc0-02b9-4a62-8534-3b2b539a88ba/full), [FFI](https://publications.ffi.no/en/component/jcar/asset/dspace:3087/04-02726.pdf)) |
| Cancelling interference between symbols when the delay spread exceeds the prefix | NVIS error floor | makes fast rungs usable on NVIS | D | no | literature: the error floor can be removed and the sufficient-prefix result approached ([Molisch et al., MERL TR2007-096](https://www.merl.com/publications/docs/TR2007-096.pdf)) |
| Acquisition over the whole frame, not the preamble alone (21) | acquisition, the limit of the robust modes | several dB of detection energy: the pilots of a control frame hold about 5.6 times the preamble's energy | D, G for the full search | no | arithmetic; the same idea as high-sensitivity satellite-navigation receivers, which use GPUs for the search ([GPU correlator](https://link.springer.com/article/10.1007/s10291-016-0516-2)) |
| Ordered-statistics decoding of the short codes (12) | weak short code under belief propagation | about 1 dB on control frames | P at order 2, D/G at order 3-4 | no | literature: at 128 bits, belief propagation on LDPC sits 0.7-1 dB from the bound and OSD close to it ([survey](https://arxiv.org/pdf/1610.00873)) |
| Coherent and iterative detection at the MFSK floor (06) | non-coherent loss | 1-2 dB | P/D | no | literature ([Valenti and Cheng](https://typeset.io/pdf/iterative-demodulation-and-decoding-of-turbo-coded-m-ary-2fu3ntd0qs.pdf), [Colavolpe et al.](https://www.eurecom.fr/publication/1598/download/cm-cairgi-040906.pdf)) |
| Neural receiver (22) | all of the estimation rows at once, and non-Gaussian noise | bounded by the genie rows above | G to train, D to run | no | literature, section 5 |
| More iterations, ensembles of decodes | decoder | about 0.1 dB | any | no | measured: not worth it |
| Salvaging pieces of failed frames | lost frames | none | — | no | measured: not viable |
| One long code across the frames of a round, decoded jointly, instead of a code per frame plus erasure coding | frames just below threshold contribute nothing today | throughput under fading, in theory the difference between counting successes and accumulating information ([Wu and Jindal](https://arxiv.org/pdf/0904.0226)) | D, G at the top rungs | yes, deep | theory |
| Learned waveform without pilots or prefix | the 2-3.4 dB structural loss | +18 % throughput reported for cellular channels ([Aït Aoudia and Hoydis](https://ar5iv.labs.arxiv.org/html/2101.08213)) | G to train | yes, complete | literature |

## 5. Neural networks

### What exists

- **Neural receivers.** A network replaces channel estimation, equalisation
  and soft demapping, and feeds the ordinary decoder. DeepRx
  ([Honkala et al.](https://arxiv.org/pdf/2005.01494)) matches a receiver
  that knows the channel while using half the pilots, and holds up at
  Doppler shifts where the classical one collapses. A version that meets
  real-time limits on a GPU, with source code, exists for 5G
  ([Wiesmayr et al., NVIDIA/ETH](https://arxiv.org/abs/2409.02912): under
  1 ms per slot, 0.7 dB from the unconstrained network). Receivers trained
  only on simulated channels have been validated over the air, with the
  finding that training on a *broad* range of simulated channels generalises
  better than matching one environment
  ([Luostari et al.](https://arxiv.org/pdf/2408.04182)); fine-tuning on
  measurements from the actual site helps further
  ([ETH 2026](https://arxiv.org/pdf/2603.09644)).
- **Learned handling of interference and non-Gaussian noise.** A two-stage
  network that cancels narrowband interference and then corrects the soft
  values gains about 0.8 dB over the best classical erasure scheme
  ([Kavvousanos et al., 2026](https://arxiv.org/abs/2607.08717)). For
  impulsive noise, learned detectors match a clipping receiver without being
  told the noise parameters
  ([Barazideh et al.](https://arxiv.org/pdf/1901.00447)), and the true soft
  value under impulsive noise is strongly non-linear, which is exactly what
  a network can learn and a Gaussian formula cannot
  ([Machine LLRning](https://arxiv.org/pdf/1907.01512)).
- **Neural decoders.** Transformers that decode block codes beat belief
  propagation on short codes
  ([ECCT acceleration](https://arxiv.org/html/2410.05911v1), CrossMPT), up
  to about a thousand bits, with reported loss of robustness when the noise
  is not what they were trained on. For Mercury only the control frame is in
  range, and ordered-statistics decoding (proposal 12) reaches the same goal
  deterministically.
- **Diffusion and other generative "denoisers".** Published gains are for
  images and other content with structure
  ([example](https://arxiv.org/html/2310.19460v2)). They work by knowing
  what the *content* should look like.
- **HF specifically.** The one neural system in regular HF use is RADE, a
  learned speech waveform by the author of the OFDM modem this project
  vendors ([Rowe and Valin](https://arxiv.org/pdf/2505.06671)). It is for
  speech. For HF data there are neural equalisers in the literature
  ([Arizona](https://repository.arizona.edu/handle/10150/661596)) and a
  patent, but no open result that could be reused as is.
- **Prediction in the literal sense.** Networks that predict the fading
  ahead of time exist ([overview](https://arxiv.org/pdf/2510.24400)); here
  they would serve the choice of rung and of the moment to transmit, not
  decoding.
- **Language models correcting text.** Using a language prior to fix
  residual errors in text is an active line
  ([example](https://ar5iv.labs.arxiv.org/html/2505.08536)).

### The boundary

A network cannot recover information that is not in the received samples.

- Payload bits that are compressed or encrypted are random; there is nothing
  to predict, and a generative model would only invent data. (Mercury's CRC
  and block check would reject it, which is the right outcome.) Winlink and
  most file traffic is compressed. Only plain text has a prior to exploit,
  and compressing the text is the better use of that redundancy.
- What is predictable is the **channel** (fading evolves smoothly, paths
  are few) and the **noise** (carriers, impulses, inverter hash). A network
  earns its keep by modelling those better than the hand-written estimator.
  Section 3.2 is therefore its budget: at most the genie's gain on Gaussian
  noise, and more only where the noise has structure that the classical
  blanker and weights (proposals 07, 08) do not capture.

### Speed is not the problem

DATAC1 is 27 carriers by 190 symbols every 4.4 s; QAM16C2 is 33 by 155 every
3.3 s. A 5G slot that the real-time receiver above handles in under a
millisecond is larger. On a GTX 1080 Ti even a very large model would run
many times faster than real time, and a model of DeepRx's size would run on
a desktop CPU. "Only a GPU can run it fast enough" does not describe
inference at HF rates. The GPU is for training.

### What makes or breaks it: training data

A network trained on the Watterson model alone would learn the Watterson
model. Three sources, combined:

1. **Synthetic signal through the project's own channel model**, across all
   presets, offsets and levels (the broad range that generalised in the
   literature).
2. **Real noise, recorded with no signal** (`MERCURY_RX_DUMP` on a quiet
   frequency next to real inverters, power lines, static), **plus synthetic
   Mercury bursts added at a chosen level.** The labels are exact because
   the burst is ours, and the noise is the real thing. This is the cheapest
   route to a receiver that knows inverter noise, and it needs no second
   station.
3. **Real sessions**, where the labels come from frames that decoded
   (re-encode them and the transmitted symbols are known), for fine-tuning.

### Safety

A learned stage goes in as one more fallback attempt behind a switch, like
proposal 09: it produces soft bits, the unchanged LDPC decoder and CRC
decide. It can never lose a frame that decodes today, and it cannot get
wrong data accepted beyond the CRC escape rate, which the parity gate
already bounds.

## 6. The GTX 1080 Ti in practice

- 11 GB, about 11 TFLOPS in single precision, no tensor cores. Ample for a
  receiver model of a few million parameters.
- It is a Pascal card. NVIDIA's 580 driver branch is the last to support it,
  and the CUDA 12 toolkit line is the last that builds for it; CUDA 13
  removes it ([Tom's Hardware](https://www.tomshardware.com/pc-components/gpus/nvidia-to-drop-cuda-support-for-maxwell-pascal-and-volta-gpus-with-the-next-major-toolkit-release),
  [NVIDIA 580 notes](https://docs.nvidia.com/datacenter/tesla/tesla-release-notes-580-95-05/index.html)).
  It keeps working on those versions. Pin the driver to 580 and use a
  training framework built against CUDA 12; check the framework's
  compatibility table before installing (not verified here).
- NVIDIA's Sionna is the reference open simulator for this kind of work, and
  its 5G LDPC and OFDM blocks are GPU-accelerated; Mercury's waveform would
  have to be re-implemented in it. The simpler path is to generate training
  data with the C modem that already exists and train a small model on it.
- Runtime use of the card inside `mercury` would add a CUDA dependency that
  almost no other station has. Keep the runtime on CPU threads and SIMD;
  use the card for training and for large bench matrices.

## 7. Recommendations

In order:

1. **Proposal 19, cyclic-prefix recycling.** Measured, cheap, no wire
   change.
2. **Proposal 23, decode-time budget.** Time the attempts on a Pi 4, stop
   hopeless decodes early, and give the receiver effort levels so the same
   build serves a Pi and a desktop.
3. **Proposal 20, decision-directed estimation.** The largest measured
   ceiling; classical, deterministic, no training data.
4. **Proposal 21, whole-frame acquisition.** The robust modes are limited by
   acquisition, not decoding.
5. **Proposal 22, neural receiver, as research on the GTX 1080 Ti**, judged
   against proposal 20 on the same bench and against the genie ceiling. Start
   with the case classical methods handle worst: real recorded noise.
6. Do **not** spend effort on more decoder iterations, ensembles of decodes,
   or salvaging pieces from failed frames.

## Reproducing

See `prototype/lab2/README.md`.
