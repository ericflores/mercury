#!/bin/sh
# Proposal 16, phase 0: does a narrow mode need to move to a clean part of
# the passband, or do the notch (nt) and the per-carrier weights (nw) cover it?
#
#   subband_phase0.sh MODE SNR3K centred|moved [trials]
#
# MODE at SNR3K (about +6 dB over its AWGN 90 % point on this bench), a CW
# carrier and then a 2-FSK interferer (45.45 baud, 170 Hz shift) on 1500 Hz at
# 0, +10 and +20 dB relative to the burst.  "centred": the mode at 1500 Hz,
# columns stock, nw, nt, nt+nw.  "moved": the mode opened at 1000 and at
# 2000 Hz (RX_BENCH_CENTRE), columns stock and nt+nw.  Both groups first run
# the no-interferer control (neutrality: the moved modes must deliver like the
# centred one).  AWGN; every column decodes the same samples.  MOVED_VARIANTS
# replaces the moved group's columns (for example with bp, the bench-only
# band-pass).
set -e
MODE=${1:?mode}
SNR=${2:?snr}
GROUP=${3:?centred|moved}
TRIALS=${4:-60}
BENCH=${BENCH:-$(dirname "$0")/rx_bench}
MOVED=${MOVED_VARIANTS:-stock,nt+nw}

run() { # centre variants tone_hz tone_db fsk_hz fsk_db label
  if [ -n "$1" ]; then export RX_BENCH_CENTRE=$1; else unset RX_BENCH_CENTRE; fi
  echo "## $7"
  "$BENCH" "$MODE" "$TRIALS" "$SNR" "$SNR" 1 awgn "$2" "$3" "$4" 0 0 "$5" "$6" | tail -n 2
}

if [ "$GROUP" = centred ]; then
  run "" stock,nw,nt,nt+nw 0 -99 0 -99 "no interferer"
  for db in 0 10 20; do
    run "" stock,nw,nt,nt+nw 1500 $db 0 -99 "CW 1500 Hz $db dB"
  done
  for db in 0 10 20; do
    run "" stock,nw,nt,nt+nw 0 -99 1500 $db "FSK 1500 Hz $db dB"
  done
else
  for c in 1000 2000; do
    run $c "$MOVED" 0 -99 0 -99 "centre $c, no interferer"
    for db in 0 10 20; do
      run $c "$MOVED" 1500 $db 0 -99 "centre $c, CW 1500 Hz $db dB"
    done
    for db in 0 10 20; do
      run $c "$MOVED" 0 -99 1500 $db "centre $c, FSK 1500 Hz $db dB"
    done
  done
fi
