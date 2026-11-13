# What modem73 does, and what it means for this programme

[modem73](https://github.com/RFnexus/modem73) (commit 4232d6e, read
2026-11-13) is a software modem for HF, VHF and UHF in a 2400 Hz channel.
Its own code is public domain and its DSP library (aicodix) carries a
permissive licence, so ideas and code may be reused. It is C++; Mercury is C.

**Nothing here was run.** The conclusions come from reading its source,
except where a measurement on Mercury is cited.

## A different kind of modem

modem73 is a packet modem: a KISS TNC for networks such as Reticulum, with
carrier sensing and no retransmission protocol of its own yet (its changelog
announces one). Every frame says what it is, and every decoder listens all
the time. Mercury is a session modem: the receiver chooses the mode and asks
for what it lacks. Several of modem73's choices follow from that difference
and do not transfer.

## Technique by technique

| modem73 | where | Mercury today | verdict |
|---|---|---|---|
| Acquire in the middle of a frame from the pilot rows, matched differentially between consecutive pilot rows; accept a frame whose head was missed and erase the missing rows; rescue backwards from the end marker | `phy/robust_modem.hh`: `pilot_scan`, `pilot_enter`, `rescue_backward` | preamble or postamble only | The principle of proposal 21, in a fielded modem: evidence that the differential pilot-row metric works. "Late join" is worth a step in 21 only where a retained copy exists (HARQ): Mercury's codes are rate 1/2, not 1/4 |
| Decode again with the symbol window shifted and with other tracking gains | `modem.hh`: `retry_decode` | one window, placed at the head of the prefix | Led to the measurement behind **proposal 26**: on disturbed NVIS Mercury's limit is where the window sits and a timing tracker that loses the burst. With both driven by the pilots' delay profile, DATAC1 goes from 12 to 58 of 60 frames at +7.8 dB |
| Impulse blanker ahead of the receive filters, on by default: scale a sample down above 6× a tracked envelope, zero it above 8× | `phy/robust_modem.hh`, `modem.hh` (`IMPULSE_BLANKER`) | none | Proposal 08. Put their thresholds on the bench as one variant |
| Noise per carrier from the pilot residuals, into the soft bits | `phy/robust_modem.hh`: `pilot_noise` | fixed `EsNodB` | Proposal 07, same idea |
| Soft bits weighted by the channel power of each tone | `modem.hh`: `PER_TONE_PRECISION` | already there (`symbols_to_llrs` scales with the amplitude estimate) | nothing to do |
| After a failed decode, erase the weakest rows (1/8, 1/4, or those under 0.3× the median) and decode again | `phy/robust_modem.hh`: `Attempt` | none | Probably small here: Mercury's soft bits already fall with the square of the estimated amplitude. Cheap to add as a variant of 07's attempt; not a proposal until measured |
| Channel estimate interpolated across four pilot rows (Catmull-Rom) | `phy/robust_modem.hh` | one estimate per modem frame | **Measured on Mercury, no gain** at 1 Hz Doppler (proposal 26, prototype section). Untested between 2 and 5 Hz |
| Polar codes with list decoding everywhere in the robust family, down to a 32-byte burst of 0.54 s on a (1024,288) code | `phy/robust_modem.hh`, `deps/aicodix/code/polar_list_decoder.hh` | LDPC with sum-product; control frame 14 bytes in 3.7 s | For proposal 14 (short control frames): bench a CRC-aided polar code with a list of 8-32 against the short LDPC code with proposal 12's ordered-statistics decoder. Short blocks are where list decoding earns its cost |
| Constellations to 4096-QAM, code rates to 5/6; its own table of the SNR each needs (64-QAM 3/4 at 19 dB, 256-QAM 3/4 at 24 dB, 1024-QAM 3/4 at 29 dB, 4096-QAM 5/6 at 35 dB) | `probe.hh`, `modem.hh` | top rung 16-QAM at rate 0.6, about 2.6 kbit/s | Reference points for proposals 15 and 25. Its own guide says to stay under 8-PSK on HF: the dense modes are for FM links |
| A robust family in 600 Hz as well as 2400 Hz | `phy/robust_modem.hh` (`RDMN-*`) | BW500 caps the ladder at DATAC3 | nothing new; Mercury's narrow rungs exist |
| Frames that describe themselves (a strongly coded first symbol with mode and callsign); all families decoded at once | `modem.hh` (meta symbol) | the receiver chose the mode | Not needed in a receiver-driven protocol, and it costs airtime in every frame |
| Carrier sensing with presets per band and a short identifying tone ahead of a frame | `csma.hh`, `tone_dcd.hh` | busy detector, off by default | Belongs to shared channels and nets. Relevant only if Mercury grows a datagram interface |
| SNR and bit error rate measured after decoding, against the re-encoded codeword | `modem.hh` | SNR from the demodulator | Already specified in proposal 10 |
| KISS over TCP | `kiss_tnc*.{cc,hh}` | VARA-style TNC protocol | A product decision (Reticulum, APRS, AX.25 hosts), not a modem improvement |

## What to take from it

1. The window study (proposal 26), which its retry list suggested and
   Mercury's own bench turned into the largest measured gain of the
   programme so far, on the channel Mercury is most used on.
2. Its blanker thresholds and its polar list decoder as bench candidates
   inside proposals 08 and 14.
3. Confidence that proposals 07 and 21 are sound: another modem arrived at
   the same mechanisms independently.
