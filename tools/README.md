# Splash animation tools

```bash
node convert_official_clawd.js
node convert_official_clawd.js --verify /tmp/verify   # + per-animation PNGs
```

Converts the official Anthropic Clawd animations archived in
`research/clawd-official/` (GIFs decoded via ImageMagick, the Laptop and
Soccer Lottie exports read directly) into a single
`firmware/src/splash_animations.h`:

- frames as bounding-box crops on the shared 55×37 art stage, one byte per
  cell into a per-animation ≤16-color RGB565 palette (index 0 = background)
- per-frame hold in ms, with consecutive duplicate frames collapsed
- a detected loop region per animation (gait cycles, scene middles) that the
  engine can hold or release for walk-to-target and timed scenes
- the eyes — transparent holes in the source GIFs — inked as `#141413`
- contrast recolors (trumpet notes → ivory, magnifier fedora → gray) and the
  sailing-loop cross-match that defines the sailing scene's loop window

See [`research/clawd-official/CLAUDE.md`](../research/clawd-official/CLAUDE.md)
for asset provenance and format details, and `--in`/`--out` to override paths.
Requires ImageMagick (`convert`/`identify` on PATH). Rebuild firmware after
running.

## Re-running

The generated header is checked in — you only need ImageMagick if you're
re-running the converter (e.g. after updating a source asset). It's a
deterministic function of `research/clawd-official/`'s contents, so a rerun
against unchanged assets should reproduce the checked-in file byte-for-byte.

## License note

This converts Anthropic's own copyrighted "Clawd" mascot art. See the
"Licensing gray area warning" in the root [`README.md`](../README.md) and
[`research/clawd-official/CLAUDE.md`](../research/clawd-official/CLAUDE.md)
for the sourcing methodology and what is/isn't confirmed used in a shipped
Anthropic surface.
