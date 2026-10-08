# Native game regression tests

`GamesTest` compiles the production Chess, Checkers, Backgammon and GameSession CPP files. The stubs replace only the hardware, input, render scheduling and SD boundary; game rules and snapshot codecs are not duplicated.

```sh
cmake -S test -B build/test
cmake --build build/test --target GamesTest -j4
ctest --test-dir build/test -R '^(GamesTest|SessionTest)\.' --output-on-failure
```

For address and undefined-behavior checks, use a separate build directory:

```sh
cmake -S test -B build/test-games-sanitize -DGAMES_SANITIZE=ON
cmake --build build/test-games-sanitize --target GamesTest -j4
build/test-games-sanitize/games/GamesTest
```

The suite covers direct and progressive captures, forced maximum captures and branch locking, combined dice movements and blocked intermediate points, bar priority, doubles, bearing off, visible AI rolls and sequential moves, a full 800 ms pause after rendering, mid-turn and final-pause recovery, restart confirmation and consumed long-press releases, castling/en-passant snapshots, CRC/metadata validation and journal recovery through SD failures.

The fake clock models display completion with a configurable duration. `RenderLock` detects recursive acquisitions and blocking render requests while locked; activity entry runs unlocked and exit/sleep hooks run under the caller's lock, matching ActivityManager. `finish()` queues a transition without destroying the current activity immediately.

The fake filesystem models short reads/writes, failed close, directory creation, rename, removal and replacement. Failed replacement removes the destination while preserving the source, exercising recovery from an interrupted FAT operation. Complete temporary snapshots take precedence over older primary and backup files.

These are host behavior tests. They do not measure ESP32 heap/stack usage or prove e-ink contrast, SD electrical behavior, touch calibration or physical button ergonomics. Those require the compiled firmware on the reader.
