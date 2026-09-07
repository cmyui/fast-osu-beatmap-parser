# fast_float

`fast_float.h` is the unmodified amalgamated header from
[fast_float v8.0.2](https://github.com/fastfloat/fast_float/releases/tag/v8.0.2).
Its MIT, Apache 2.0, and Boost license notices are included in the file.

The standalone runtime uses it for the long-decimal/exponent fallback that
otherwise needs libc's `strtod`. The main parser's existing decimal arithmetic
stays unchanged so ordinary values preserve master’s exact floating-point bits.

Verified against the upstream release asset, SHA-256:
`0883786faf4d98a2ffe97f29d839e2651ac29464a84c24f075a1d4fac8151711`.
