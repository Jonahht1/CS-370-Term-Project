# Mechanism B bench tests, 2026-11-15

**Live, manual bench tests, not replayed or synthetic data.** Cole ran these
on the Raspberry Pi 5 with the physical sensors wired on the breadboard, not
yet mounted in a doorway. Their only purpose was to show that both input
modes of `tools/sensortest/sensortest.c` record changes from all three
sensors and stop cleanly. They are **not** accuracy, latency, CPU, or
debounce measurements, and they do not compare polling with edge events.
Each session used different, unscripted hand movements, so their event
counts and timings cannot be compared with each other.

| File | Command (run in `tools/sensortest/`) | Mode |
|---|---|---|
| `mechb-polltest-2026-11-15.log` | `./sensortest -p polltest.log` | polling, 10 ms default interval |
| `mechb-edgetest-2026-11-15.log` | `./sensortest edgetest.log` | edge events (default) |

Both logs are unchanged byte-for-byte copies of the files sensortest wrote:

```
223c09a79f87225c12c22b37c3efe67342dd5be1e6ac8131a8aa8c4cbd1c8b5c  mechb-polltest-2026-11-15.log
9af6f21020d39f6cffc4fe6bafed441a657ce98e8c2e91f98e0e8d38a8e7392d  mechb-edgetest-2026-11-15.log
```

## What the logs show

- Each log holds one session: a `start` marker, a `mode` line, three
  `initial` states, the recorded changes, and a `stop` marker. Both
  sessions were stopped with Ctrl+C.
- Polling session (14:03:58–14:05:14): 24 `polled` changes (12 `beam_hi`,
  8 `beam_lo`, 4 `door`) and no edge-event lines.
- Edge-event session (14:09:44–14:10:30): 67 `rising`/`falling` events
  (53 `beam_hi`, 10 `beam_lo`, 4 `door`).
- The rapid `beam_hi` changes in the edge-event log, mostly between
  14:10:07 and 14:10:11, happened while Cole was moving a hand through the
  beam and adjusting the sensors. They are expected results of that
  handling.
- Both sessions began with both beams reading `blocked`. This is recorded
  as-is and was not investigated in these tests.
- Polling timestamps are when a change was observed at a sample, not when
  it physically happened. Door changes in polling mode are recorded only
  after the software debounce confirms them.
