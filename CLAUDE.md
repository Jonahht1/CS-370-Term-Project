# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project status

CS 370 (Operating Systems, CSU) term project for a two-person team. The assignment spec is not in the repo; the team will provide relevant requirements as work proceeds. The device is a camera-free doorway monitor that distinguishes person-sized from pet-sized entries (see `PROBLEM.md`). Planned mechanisms are B (interrupt vs. polling) and E (multi-process + supervisor). Keep this file current as the design, commands, and ownership map evolve. The spec requires this file to be checked in and kept current across milestones.

The deliverable is a Raspberry Pi device with at least two sensors, running software the team wrote, that solves a real problem for a specific, nameable user. The test the spec keeps applying is: "who wants this, and why doesn't a phone app already solve it?"

## Hard constraints (graded; do not violate)

- **No model in the product.** The shipped code may not call any LLM, cloud inference API, or hosted model. It may not embed a pretrained model unless the design doc petitions for one and the petition is approved. All detection, diagnosis, and decision logic must be the team's own code: statistics, DSP, state machines, fusion rules. Math-primitive libraries (FFT, linear solvers) are fine. Claude Code is required in the *workflow*. It must never be part of the *product*.
- **No network dependence.** The device must work with the network unplugged. The only network activity allowed is serving its own LAN interface (CLI or dashboard). A grader should be able to confirm this from the source tree in about a minute.
- **Intelligence beyond a threshold.** If a single `if` could do it, it doesn't count.
- **Two sensors that cooperate.** They must be physically distinct and fused into one decision, correlated against each other, or feed one pipeline whose behavior depends on both. Two independent demos don't count.
- **At least two "below the application layer" mechanisms**, implemented by the team and measured in the evaluation:
  - A: kernel module / character driver
  - B: interrupt-driven input vs. polling comparison
  - C: SCHED_FIFO/RR real-time path
  - D: custom crash-consistent storage (ring buffer, append log, or FUSE)
  - E: multi-process sensors + IPC hub + supervisor that survives any child dying
  - F: high-rate no-drop SPSC/lock-free ring
- **Systems core in C17 or C23**, clean under `-Wall -Wextra -Werror`. ASan/valgrind findings cap a component's score at 50%. Other languages are allowed only in the interface layer, and no graded mechanism may live there.
- **48-hour unattended soak.** Logs must show a timestamped startup, a heartbeat at least hourly (liveness, RSS, cumulative event counts), at least one injected fault (unplugged sensor or `kill -9`) with detection, degradation, and recovery visible, and an orderly end state. Design every daemon to be supervisable and leak-free over days.
- **Label replayed or synthetic data everywhere it appears** (logs, report, demo). The soak and the live demo must use live sensors.

## Working rules for Claude

- Each partner is graded individually on defending their own code line by line and making live modifications to it. Keep diffs small, explain the reasoning, and plan before multi-file or algorithmic changes.
- For hardware problems, work from pasted evidence (`dmesg`, timing captures, `/proc/interrupts`, `i2cdetect`) rather than guessing about sensor behavior. Hardware details are where the agent tends to be confidently wrong.
- Pi GPIO is 3.3 V and not 5 V tolerant, and the Pi has no analog inputs. Flag any wiring suggestion that involves 5 V parts (e.g., the HC-SR04 echo needs a voltage divider) or analog sensors (they need an ADC such as the MCP3008).
- Never weaken, skip, or delete a test to make a suite pass.
- Git: aim for 40+ meaningful commits with both partners well represented. Never make a single squash or "final submission" commit.

## Required repo artifacts (by M5)

- `Makefile` and a `README.md` that take a TA from a clean Pi to the running system
- `PROBLEM.md` (M1 memo; template in the spec's Appendix C)
- `DESIGN.md` (M2), with a changelog of what the M2 version got wrong
- `EVALUATION.md`
- Raw soak logs
- Per partner: `PROMPTLOG.md`, `REFLECTION.md`, and raw `.jsonl` transcripts copied out at every milestone M1–M5. Transcripts are purged after 30 days by default.

## Hardware (Pi 5, `/dev/gpiochip0`)

| Sensor | GPIO (pin) | Bias | Debounce | HIGH / LOW |
|---|---|---|---|---|
| `beam_lo` IR break-beam | 17 (11) | disabled | none | clear / blocked |
| `beam_hi` IR break-beam | 22 (15) | disabled | none | clear / blocked |
| `door` magnetic reed | 27 (13) | pull-up | 50 ms | open / closed |

- The beams will be mounted stacked vertically (`beam_lo` below `beam_hi`; not physically mounted yet) to tell human-sized from pet-sized activity. They are **not** used for movement direction.
- Beam receivers reach the GPIO through 100kΩ voltage dividers, so internal pulls must stay disabled. The reed switch only shorts to ground, so it needs the pull-up.
- Beams are not debounced, because delaying edges would distort when both beams are blocked together.

## Current code

- `tools/beamtest/`: original single-beam test (GPIO17), kept as a reference.
- `tools/sensortest/`: monitors all three sensors in one libgpiod v2 line request and prints timestamped edge events. Physically verified on the Pi.
- Not implemented yet: persistent storage, classification, multi-process/IPC.

Build and test (needs libgpiod v2 headers; `make test` needs the `gpiod` tools and runs on the Pi):

```
cd tools/sensortest
make          # build ./sensortest (Ctrl+C to stop)
make asan     # build ./sensortest-asan (ASan + UBSan)
make test     # check applied bias/debounce via gpioinfo, require clean ASan exit
```

## Architecture / Ownership

TBD. Fill in once the design is chosen. Appendix B of the spec suggests `make`, `make test`, `make asan`, `make memcheck`, `make soakcheck`, and `make deploy PI=pi@<host>`, with an ownership map by source directory.
