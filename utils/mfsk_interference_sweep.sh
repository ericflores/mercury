#!/bin/bash
# utils/mfsk_interference_sweep.sh -- the MFSK floor against a carrier and
# impulses, as received and after the receive thread's notch and blanker.
#
#   mfsk_interference_sweep.sh <mode> <trials> <snr3k> <tone_hz> <tone_rel_db|none> <impulses_per_s|0> <impulse_rel_db>
#
# <mode> is a Mercury MFSK mode number (100 the floor, 103 the 500 Hz floor).
# Each trial sends one CRC-valid frame through utils/watterson_test (white
# noise at <snr3k>, a carrier <tone_rel_db> relative to the frame's power, and
# noise bursts of 2 ms whose rms is <impulse_rel_db> over it), then decodes
# what came out four ways with utils/mfsk_burst_file: as received, after the
# carrier notch, after the blanker at 6, after both (utils/rx_prefilter: the
# same modules mercury's receive thread runs, modem/rx_notch.c and
# modem/rx_blanker.c).  Three seconds of the channel precede the frame, as a
# carrier on the air has been there for a while when a frame arrives.
#
# The channel gain is 0.25 (G=<gain> in the environment changes it) to leave
# room for interference above the signal without clipping; impulses 25 dB or
# more over the signal need G=0.02.  The noise density follows the gain, so
# <snr3k> is what it says (the -11.05 is mfsk_channel_sweep.sh's calibration
# of the frame's level).
#
# Example: a carrier 10 dB over the floor's signal at 9 dB over its threshold
#   utils/mfsk_interference_sweep.sh 100 30 -5 1500 10 0 0
set -u
if [ $# -ne 7 ]; then sed -n '2,24p' "$0"; exit 1; fi
MODE=$1; T=$2; S=$3; THZ=$4; TREL=$5; IR=$6; IREL=$7
HERE=$(cd "$(dirname "$0")" && pwd)
TOOL="$HERE/mfsk_burst_file"; WT="$HERE/watterson_test"; PF="$HERE/rx_prefilter"
[ -x "$TOOL" ] || make -C "$HERE" mfsk_burst_file >/dev/null || exit 1
[ -x "$WT" ] || make -C "$HERE" watterson_test >/dev/null || exit 1
[ -x "$PF" ] || make -C "$HERE" rx_prefilter >/dev/null || exit 1
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
G=${G:-0.25}
"$TOOL" tx "$MODE" 1 "$W/b.raw" "$W/f.raw"
LVL=$("$PF" level "$W/b.raw")
SIG=$(python3 -c "import math; print($LVL + 20*math.log10($G))")
No=$(python3 -c "import math; print(-($S) - 11.05 + 20*math.log10($G))")
OPT=""
[ "$TREL" != none ] && OPT="$OPT --tone $THZ,$(python3 -c "print(round($SIG + 3.01 + $TREL, 2))")"
[ "$IR" != 0 ] && OPT="$OPT --impulses $IR,$(python3 -c "print(round($SIG + $IREL, 2))")"
ok0=0; ok1=0; ok2=0; ok3=0
for t in $(seq 1 "$T"); do
  "$TOOL" tx "$MODE" "$t" "$W/b.raw" "$W/f.raw"
  "$PF" pad 3 "$W/f.raw" "$W/p.raw"
  MERCURY_WATTERSON_SEED=$t "$WT" "$W/p.raw" "$W/n.raw" --No "$No" --gain $G $OPT 2>"$W/err" ||
    { echo "the channel tool refused its options (interference above full scale? lower G):"; cat "$W/err"; exit 1; }
  ok0=$((ok0 + $("$TOOL" rx "$MODE" "$t" "$W/n.raw" 2>/dev/null)))
  "$PF" filter "$W/n.raw" "$W/x.raw" nt 2>/dev/null;    ok1=$((ok1 + $("$TOOL" rx "$MODE" "$t" "$W/x.raw" 2>/dev/null)))
  "$PF" filter "$W/n.raw" "$W/x.raw" b6 2>/dev/null;    ok2=$((ok2 + $("$TOOL" rx "$MODE" "$t" "$W/x.raw" 2>/dev/null)))
  "$PF" filter "$W/n.raw" "$W/x.raw" nt b6 2>/dev/null; ok3=$((ok3 + $("$TOOL" rx "$MODE" "$t" "$W/x.raw" 2>/dev/null)))
done
echo "mode=$MODE SNR3k=$S tone=$THZ Hz ${TREL} dB impulses=$IR/s ${IREL} dB: as received $ok0, notch $ok1, blanker $ok2, both $ok3 of $T"
