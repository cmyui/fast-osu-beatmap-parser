# fast_float

`fast_float.h` is the unmodified amalgamated header from
[fast_float v8.2.10](https://github.com/fastfloat/fast_float/releases/tag/v8.2.10).
Its MIT, Apache 2.0, and Boost license notices are included in the file.

The standalone runtime uses it for the long-decimal/exponent fallback that
otherwise needs libc's `strtod`. The main parser's existing decimal arithmetic
stays unchanged so ordinary values preserve master’s exact floating-point bits.
