# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project status

CS 370 (Operating Systems, CSU) term project for a two-person team. The full spec is `CS370-TermProject.pdf`. Read it before making design decisions. **No code exists yet, and the device hasn't been chosen.** The team is still in ideation (M0/M1). Update this file when the device, sensors, mechanisms, build commands, and ownership map are decided. The spec requires this file to be checked in and kept current across milestones.

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

## Commands / Architecture / Ownership

TBD. Fill in once the design is chosen. Appendix B of the spec suggests `make`, `make test`, `make asan`, `make memcheck`, `make soakcheck`, and `make deploy PI=pi@<host>`, with an ownership map by source directory.
