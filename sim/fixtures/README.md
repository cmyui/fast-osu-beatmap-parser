# Engine regression fixtures

One file per bug that cost a real debugging session, so a regression fails in a
second rather than thousands of files into the corpus. Run with:

```sh
make rtl-test-engine RTL_CORPUS=sim/fixtures
```

| file | what it pins |
|---|---|
| `a_bracket_title.osu` | a metadata value starting with `[`. The line iterator's outputs were combinational on the shared memory window, so when the engine moved the cursor to read this value the stalled iterator reclassified the line as a section header and corrupted its section register. |
| `b_noncombo_colour.osu` | a `[Colours]` line that is not `Combo`. Finishing a line with nothing to report used to return straight to idle, but only a terminal state releases the line — so the iterator re-offered it forever. |
| `c_long_background.osu` | a quoted background filename longer than one 64-byte window, plus storyboard lines. `trim`/`strip_quotes` originally required the span to fit a window and punted otherwise, which punted the common case. |
| `d_punt_prefix.osu` | negative, 4-digit and decimal coordinates: prefixes the SIMD path rejects and the lenient scalar path accepts, so they must defer to the host and still land in order. |
| `e_slider_extras.osu` | sliders with three, two and zero trailing extras. The extras comma scan walked its own pointer while the read port was still addressed by the cursor, so positions were computed against one base and used against another. |
| `f_spinner_hold.osu` | spinner vs mania hold sample separators — a hold uses `:` and a spinner `,` — and a spinner with no sample at all. |
