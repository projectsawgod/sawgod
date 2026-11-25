# Debug log

What went wrong during play, and how badly. One small text file per session, with startup timings and problems.

## Where

`<data>/logs/sawgod-YYYYMMDD-HHMMSS-mmm.log`, beside `sawgod.toml` (docs/CONFIG.md says where the data folder
is; on Linux usually `~/.local/share/sawgod/sawgod/logs/`). The newest 10 files are kept.

`SAWGOD_DEBUG_LOG_DIR=<folder>` picks another folder and turns the 10-file limit off.

## What a file holds

```
SawGod 0.0.1-dev, 09:14:03
# WARNING: cosmetic, or a fallback took over; fix if cheap. ERROR: a feature failed or the game looks wrong; ...
00:41:12 ERROR picture: the picture has been black for 15 seconds [map 1, GB, service]
00:52:30 WARNING stall: a frame took 2410 ms [map 88, Enhanced, field]
repeated 3 times: WARNING stall: a frame took 2410 ms
session end: 0 critical, 1 errors, 3 warnings
```

Each line: time since the session started, severity, area, message, and where the game was (map, Graphics level,
screen). The severities say how badly it needs fixing:

| Severity | Meaning | Fix |
|---|---|---|
| WARNING | cosmetic, or a fallback took over (a missing optional resource, a slow frame) | if cheap |
| ERROR | a feature failed or the game looks wrong (Enhanced unavailable, GB art missing, a stuck picture) | before release |
| CRITICAL | a crash or lost progress (a save that couldn't be written, the game couldn't start) | first |

A session that ends without its `session end:` line (killed, hung, power lost) is reported as an ERROR at the start
of the next one. A crash writes `CRITICAL crash: <what> [<where>]` and `session end: crashed`.

## Startup timings

Every logged launch includes `startup:` lines, measured with a monotonic clock in milliseconds since
process creation (Linux process creation has clock-tick resolution; other desktop platforms without a
process-creation clock use native initialization). Android includes process/activity start, bundled-asset
installation and native library loading, followed by the same native stages as desktop. Early milestones
are buffered until the log opens. Stage durations can overlap: background audio/effect loads and nested
resource stages must not be added together.

`startup: total N ms; top stages ...` ends at the first game/splash frame presented. A fresh import includes
extraction and any time spent choosing the ROM. Background completions can follow this line; they never
skip simulation or animation frames. Archive verification, map banks and Vulkan pipeline-cache hit/miss
are logged explicitly. Timings are informational and do not count as warnings. Scripted performance runs
can enable the same log with `SAWGOD_DEBUG_LOG=1`; timings also go to stderr, and Android's Java milestones
go to logcat. No GPU readback or extra synchronization is added for timing.

## Staying small

- A message repeated is written once and counted; the counts close the file.
- A file stops at 256 KB ("log full"); after that messages are only counted. A CRITICAL still fits.
- 10 files are kept: at most a few megabytes.
- Paths have the home folder replaced by `~`.

## What reports

- Every warning and failure the app already printed: resources, text, config, GB art, GPU features, saving
  (`sawgod::debug_log::warnf/errorf/criticalf`, src/engine/debug_log.h; they also print to stderr as before).
- What the game can't report itself (src/app/debug_watch.h): a frame taking 2 s or more while the window is on
  screen, the picture black for 15 s of game time, and a screen handoff holding its old picture for 5 s.
- Crashes: invalid memory access, aborts, arithmetic faults, illegal instructions and uncaught exceptions.

## When

Real play only. Scripted runs (`--frames`, `--inputs`, `--shot-*`, `--audio-review`) write no log, so tests never
touch the player's files; `SAWGOD_DEBUG_LOG=1` forces one (into that run's own data folder), `SAWGOD_DEBUG_LOG=0`
turns it off.
