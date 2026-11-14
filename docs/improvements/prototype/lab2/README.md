# Lab prototype for the compute research (reference only, not built)

Scratch code behind `../../COMPUTE-AND-NEURAL-RESEARCH.md`, 2026-11-13. It
applies on top of the commit that added proposal 09 (`freedv_set_chanest_alt`).

| file | what |
|---|---|
| `lab2-prototype.diff` | hooks in `modem/freedv/ofdm.c` and `freedv_700.c`: cyclic-prefix recycling (`mx_cprec`), a genie channel estimate that knows the transmitted symbols (`mx_genie`), an ensemble of perturbed LDPC decodes (`mx_ens`), an iteration-limit override (`mx_iter`), and a copy of the decoder's output for failed frames (`mx_fail_bytes`) |
| `lab2.c` | the harness; every variant decodes the same samples |
| `gap.py` | the finite-length bounds of the research note's first table (needs numpy) |
| `results/` | raw output |

## Rebuild

```sh
git worktree add --detach /tmp/lab HEAD
cd /tmp/lab
git apply docs/improvements/prototype/lab2/lab2-prototype.diff
make -C modem/freedv libfreedvdata.a -j8
cp docs/improvements/prototype/lab2/lab2.c utils/ && cd utils
gcc -w -O2 -std=gnu11 -pthread -D_GNU_SOURCE -I../common -I../modem -I../modem/mfsk \
    -I.. -I../modem/freedv -o lab2 lab2.c bench_common.c chanutil.c \
    ../common/watterson.c ../common/mercury_prng.c ../modem/freedv/libfreedvdata.a -lm
```

## Variants

`lab2 MODE trials lo hi step channel variants`, variants a comma list, each
a `+`-joined set of exact tokens:

| token | meaning |
|---|---|
| `stock` | nothing |
| `t4`, `t4f3`, `t4f5` | proposal 09's second attempt |
| `cp1` | cyclic-prefix recycling, tail side only |
| `cp2` | both sides, 4 samples of guard at the head of the prefix |
| `cp3` | both sides, 20 samples (2.5 ms) of guard |
| `genie` | channel estimate from every symbol of the frame plus the four pilot rows |
| `genie2` | the same without the previous and future pilot rows |
| `miN` | LDPC iteration limit N (stock is 100) |
| `ensN` | N perturbed decodes after a failed one (`ENS_SIGMA`, default 0.3) |

`PIECES=1` prints, for frames whose stock decode failed, the residual bit
error rate and how many 24-byte pieces of the decoder's output were entirely
right.

Recycling and the genie act on both decode attempts of a variant, because
they change the FFT input or the alternative estimate, not the attempt list.

## A warning about some result files

`results/R4_*`, `S2_*` and `S4_datac1[56]_all.txt` were produced before the
harness matched tokens exactly. In them a column headed **`it400` is in fact
`t4` with 400 iterations**, because the substring `t4` matched. Columns
headed `it150`, `it200`, `it300`, `it800`, `it1600` are what they say
(iterations only), and so is everything in `T1_datac1_decoder_only.txt`,
which is the clean iteration test (`mi400`, `mi1600`).

## Commands behind the tables

```sh
./lab2 DATAC1  100  -2   0 0.5 awgn      stock,t4f3,t4f5,t4f3+cp2,t4f5+cp2          # R1
./lab2 QAM16C2  60   6 7.5 0.5 awgn      stock,cp2,t4f3,t4f3+cp2                    # R2
./lab2 DATAC1  100 -34 -32   2 ccir-poor stock,cp1,cp3,t4f3,t4f3+cp1,t4f3+cp3       # R3
./lab2 DATAC1   60 -3.5 -1.5 0.5 awgn    t4f5+cp2,genie+t4+cp2,genie2+t4+cp2        # G1
./lab2 DATAC1   60 -32 -28   2 ccir-poor t4f3+cp3,genie+t4+cp3,genie2+t4+cp3        # G2
./lab2 DATAC1   60 -40 -38   2 itur-mdv  stock,t4f3,genie+t4,genie2+t4              # G3
./lab2 QAM16C2  40 4.5 6.5 0.5 awgn      t4f3+cp2,genie+t4+cp2                      # G4
./lab2 DATAC1   80 0.75 1.0 0.25 awgn    stock,mi400,mi1600,ens50                   # T1
PIECES=1 ./lab2 DATAC1 100 0.5 0.5 1 awgn stock                                     # S4_pieces
python3 gap.py
```
