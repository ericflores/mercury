# Prototype (reference only, not built)

Scratch code from the investigation of 2026-11-13. It is kept so the first
measurements can be reproduced and so the real implementation has a worked
example. **Do not merge it as is**: it uses process-wide globals, has no
tests, and its variant numbering is ad hoc.

| file | what |
|---|---|
| `rxlab-prototype.diff` | hooks in `modem/freedv/ofdm.c` and `freedv_700.c`: alternative channel estimators, per-carrier noise residuals and weights, an `EsNodB` override |
| `rxlab.c` | the harness: one burst per trial, impairments added to one buffer, every variant decodes that buffer |
| `results/*.txt` | raw output of the runs quoted in issues #7, #8 and #9 |

## Rebuild it

```sh
git worktree add --detach /tmp/exp HEAD        # commit 237481e or later
cd /tmp/exp
git apply docs/improvements/prototype/rxlab-prototype.diff
make -C modem/freedv libfreedvdata.a -j8
cp docs/improvements/prototype/rxlab.c utils/
cd utils
gcc -Wall -O2 -std=gnu11 -pthread -D_GNU_SOURCE -I../common -I../modem \
    -I../modem/mfsk -I.. -I../modem/freedv -o rxlab rxlab.c bench_common.c \
    chanutil.c ../common/watterson.c ../common/mercury_prng.c \
    ../modem/freedv/libfreedvdata.a -lm
```

## Run it

```
rxlab MODE trials lo hi step chan [variants] [tone_hz tone_db] [imp_rate imp_db]
```

- `chan` is `awgn` (then lo/hi are SNR3k in dB) or a Watterson preset name
  (then lo/hi are the noise density `No`; the achieved SNR3k is printed).
- `variants` is a comma list; each is letters followed by numbers:
  `c<n>` channel estimator, `n1` per-carrier noise weights, `b<k>` impulse
  blanker with threshold k, `s<dB>` override of the fixed `EsNodB`.
- Each cell is `acquired/delivered`; the last column `any` counts trials in
  which at least one variant delivered.

Estimator numbers (`c`):

| n | time weights over previous/this/next/future pilot | across carriers | phase-slope removal |
|---|---|---|---|
| 0 | stock: this, next (1,1) | none | n/a |
| 2 | 1,1,1,1 | none | n/a |
| 5 | 1,3,3,1 | none | n/a |
| 6 | this, next | 1-2-1 | yes |
| 7 | 1,3,3,1 | 1-2-1 | yes |
| 9 | 1,3,3,1 | 1-2-3-2-1 | yes |
| 1, 3 | as 6 and as 2+1-2-1 but **without** slope removal: broken, kept only to show the failure |

The commands behind the quoted tables:

```sh
./rxlab DATAC1  100  -1   2 1 awgn      c0,c5,c6,c7,c9      # results/A
./rxlab DATAC1  100 -38 -32 2 ccir-poor c0,c5,c6,c7,c9      # results/B
./rxlab DATAC15 200 -12 -10 1 awgn      c0,c2,c5,c3         # results/r1 (c3 column is the broken variant)
./rxlab QAM16C2  50   7   9 1 awgn      c0,c5,c6,c7,c9      # results/Q1
./rxlab QAM16C2  50 -48 -44 2 ccir-poor c0,c5,c6,c7,c9      # results/Q2
./rxlab DATAC1   60   5   5 1 awgn c0,c0n1,c7,c7n1 1650 -3  # results/D (tone at -15,-9,-3,+3)
./rxlab DATAC1   50   5   5 1 awgn c0,c0b4,c0b6 0 -99 3 25  # results/H (impulses +15,+25,+35)
./rxlab DATAC17  60   3   4 1 awgn c0,c0s0,c0s6,c0s9,c0s12,c5   # results/E
./rxlab DATAC15 150 -11 -11 1 awgn c0,c0s-2,c0s0,c0s1.5,c0s5    # results/E2
./rxlab DATAC1   80   1   1 1 awgn c0,c0s0,c0s1.5,c0s5,c0s7     # results/E3
```

## Known weaknesses of the prototype

- One RNG seed; 50-200 trials per point.
- The tone level is relative to total signal power; one frequency only.
- Impulses are 2 ms bursts of white noise, not real static.
- Edge carriers get an asymmetric smoothing kernel.
- The slope tracker is a simple leaky sum; with 3 carriers it has only two
  products per frame and was not validated.
- No frequency offset, no NVIS or flutter presets, no DATAC16/DATAC3/DATAC4.
