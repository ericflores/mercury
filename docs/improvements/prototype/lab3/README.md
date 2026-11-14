# Lab prototype: the FFT window on long multipath (reference only, not built)

Scratch code behind `../../26-window-placement.md`, 2026-11-13. It applies
on top of commit 60bb499 (proposal 09 present, proposal 19 not yet).

| file | what |
|---|---|
| `lab3-prototype.diff` | hooks in `modem/freedv/ofdm.c` and tokens in `utils/rx_bench.c`: the **final** version (the tracker) |
| `results/tracker/` | output of that version: the tables of the specification |
| `results/forced-window/` | fixed window positions and time-interpolated estimates |
| `results/profile-on-stock-grid/` | first version: window chosen per frame from a tapered profile, stock tracker in charge of the frame grid |
| `results/tracker-tapered/` | the tracker with its own grid but paths taken as local maxima of a tapered transform |

The code of the two intermediate versions was not kept; their output is
here because the specification cites it as "tried, not enough".

## Rebuild

```sh
git worktree add --detach /tmp/lab3 60bb499
cd /tmp/lab3
git apply docs/improvements/prototype/lab3/lab3-prototype.diff
make -C modem/freedv libfreedvdata.a -j8 && make -C utils rx_bench
```

## Tokens

`rx_bench MODE trials lo hi step channel variants`; a variant is a
`+`-joined set of exact tokens.

| token | meaning |
|---|---|
| `stock`, `t4`, `t4f3`, `t4f5` | as in the tree |
| `tpN` | FFT window forced to start N samples after the tracked timing estimate (stock starts at 4 and may slide to `ncp − 4`), with ±4 samples of hysteresis |
| `tpa` | the tracker: paths by matching pursuit on the pilot rows (`mx_detect`), tracks and placement (`mx_place`), and the frame grid driven by the placed window |
| `li` | second attempt's estimate interpolated linearly between this and the next pilot row, per data row |
| `cr` | the same with Catmull-Rom weights over four pilot rows |
| `ti` | proposal 09's four-pilot weights with a time slope (`1−t, 2−t, 1+t, t`) |

`tpN` and `tpa` change the **whole** receiver of that variant, first attempt
included: a column is "this receiver instead of stock", and the `any` column
is what a list of attempts holding all of them would deliver. `li`, `cr`
and `ti` act on the second attempt only.

`MXDBG=1` prints, per frame, the tracks (lag, share of power, age), the
shift chosen and its cost, and the stock tracker's `timing_est`.
`MXK` and `MXTHR` override the two detection thresholds (4 and 0.1).

## Why stock loses a burst on this channel

From the first version, where the stock tracker still owned the frame grid
(`te` is `timing_est`, `sp` is `sample_point`, lags in samples). The window
is locked on the early path; the late one grows; at frame 11 the late path
is the stronger and from there the correlator has nothing inside its ±5 ms:

```
frame  9  paths -97(0.29) -41(0.71)   te   1  sp  6
frame 10  paths -97(0.42) -41(0.58)   te   2  sp  6
frame 11  paths -97(0.58) -41(0.42)   te  -8  sp  6
frame 13  (one path left in view)     te -15  sp  6
frame 14                              te -35  sp  6
frame 15  nothing stands out          te -26  sp 18     <- grid shifted by 44
frame 16  nothing stands out          te  18  sp 62     <- and again
frame 20  nothing stands out          te -39  sp  5
frame 23  nothing stands out          te -49  sp -5
...        to the end of the burst (38 frames), at +9 dB SNR
```

The tracker on the same burst holds both paths (−2 and −58, then 10 and −46
when their powers swap) for all 38 frames, and the frame is delivered.

## A caveat on pairing

Variants with different windows do not see exactly the same noise samples,
so on white noise at a cliff two placements can differ by a few frames in a
hundred for no other reason. The tracker leaves a single path's window
where stock put it, so its white-noise rows are comparable (165 against 170
of 200).

## Commands behind the tables

```sh
# results/tracker (the specification's tables)
./rx_bench DATAC1  60 -38 -34 2 itur-mdv  stock,tp44,tpa,t4+tpa      # N1
./rx_bench DATAC1  40 -48 -44 4 itur-mdv  stock,tp44,tpa,t4+tpa      # N2
./rx_bench DATAC3  60 -42 -38 4 itur-mdv  stock,tp44,tpa,t4+tpa      # N3
./rx_bench DATAC1 200 0.5 1.0 0.5 awgn    stock,tpa                  # A1
./rx_bench DATAC3 100 -4.5 -4.0 0.5 awgn  stock,tpa                  # A2
./rx_bench DATAC1  60 -34 -32 2 ccir-poor stock,tpa,t4f3,t4f3+tpa    # P1
./rx_bench QAM16C2 40 -48 -44 4 ccir-poor stock,tpa,t4f3,t4f3+tpa    # Q1

# results/forced-window
./rx_bench DATAC1 30   0   1 1 awgn      stock,tp4,tp24,tp44,t4f3,t4f3+tp44,t4+ti,t4f3+ti,t4+li,t4+cr    # A1
./rx_bench DATAC1 60 -38 -36 2 itur-mdv  stock,tp24,tp44,tp52,t4,t4+tp44,t4+ti,t4+ti+tp44,t4+cr          # N1
./rx_bench DATAC1 40 -48 -44 4 itur-mdv  stock,tp44,tp52,t4+tp44                                         # N2
./rx_bench DATAC1 60 -34 -32 2 ccir-poor stock,tp24,tp44,t4f3,t4f3+tp44,t4f3+ti,t4+ti,t4+cr,t4f3+ti+tp44 # P1
```

The runs are reproducible: a repeat with the same arguments gave the same
file.
