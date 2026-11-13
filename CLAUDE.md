# Notes for coding agents working on this fork

Mercury is an HF data modem (C, gnu11): an OFDM/MFSK physical layer vendored
from codec2 with local changes, and a receiver-driven, erasure-coded ARQ
("carousel") on top. Upstream is Rhizomatica/mercury, branch `mercuryv2`.

## What this fork is for

An improvement programme tracked in GitHub issue #1 of this fork: independent
proposals plus the bench tooling they share, one sub-issue each (#2 to #17
and #19 to #23). The working specifications are in the repository:

1. **`docs/improvements/README.md`**: the handbook. Read it first: order of
   work, the four rules, repository map, build and test commands, how to add
   a switch, measurement method, wire-format constraints, and a list of
   pitfalls that already cost time.
2. **`docs/improvements/NN-*.md`**: one specification per proposal (NN is
   the issue number): facts with file and line anchors, design, steps,
   tests, acceptance criteria.
3. **`docs/improvements/prototype/`**: the scratch prototype and raw results
   behind the first measurements. Reference only.
4. **`docs/improvements/COMPUTE-AND-NEURAL-RESEARCH.md`**: what more compute
   (desktop, GPU, neural networks) can and cannot buy, with measurements,
   including ideas already tried that did not work.

Work one proposal at a time, in the order of the handbook's table, unless
the user says otherwise. Start with `00-bench.md`.

## Rules that are not negotiable

- Every feature has a switch and is **off by default**; with it off the
  build must behave exactly as before, and a test must prove that.
- Receiver-only features must work against an unmodified peer. Wire changes
  are negotiated and need the spec (`docs/CAROUSEL-SPEC.md`) and the TLA+
  models (`specs/`) updated in the same change.
- Nothing is claimed without a measurement made with the project's
  instruments (`utils/`, `tests/sim/`, `tests/integration/`). Report what was
  run, the trial counts and the caveats. If a result is worse than hoped,
  say so; several plausible ideas in this project's history lost on the
  bench.
- Do not retune constants in `datalink_arq/carousel.c` or
  `datalink_arq/arq_protocol.h` on intuition. Each cites the simulation or
  on-air case that set it.
- Line numbers in the specifications drift. Re-locate every anchor with
  `grep` before editing.

## Everyday commands

```sh
make -C modem/freedv libfreedvdata.a -j8   # modem library
make -C utils                               # bench tools
make -C tests test                          # all unit tests
make -C tests SANITIZE_ASAN_UBSAN=1 test    # as CI's sanitizer job
make && make integration-test               # binary + Go integration tests
```

## Conventions

- Code, comments, documentation, commit messages and issues are in English.
  The owner of this fork writes in Spanish: answer them in Spanish.
- Commit subject: `area: what changed` (for example `utils: rx_bench, a
  paired multi-variant receiver sweep`). One logical change per commit.
- Comments explain why, with the measurement that justified the choice.
- When a proposal lands: tick its task list in the issue, update the status
  column in `docs/improvements/README.md`, and add a findings document under
  `docs/` in the style of `docs/CLIPPED-QAM16C2-FINDINGS.md`.
- Development branch: `claude/mercury-adaptive-modem-o5hsi7`.
