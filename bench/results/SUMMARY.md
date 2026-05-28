# Three-program benchmark — token cost + runtime

All sources live under [`bench/perf/<lang>/`](../perf/). Programs:

- `wordcount` — script: split repeated text into ~4.5M words, count
  frequencies in a hashmap, print unique count and `"the"` count.
- `jsonapi` — backend: filter 50000 user records (`age >= 40`), format
  each as a JSON record, print count and total response byte length.
- `primes` — algorithm: count primes < 500001 via trial division.

Token counts use OpenAI's `cl100k_base` tokenizer (GPT-4 family).
Runtime measured with `hyperfine --warmup 1 --min-runs 5` on Apple
Silicon. All compiled languages: `clang -O2` / `rustc -O` / `go build`
/ `javac`.

## Source tokens (lower is better)

| program   | AiLang | Python | JS  | Rust | Go  | Java | C   |
|-----------|-------:|-------:|----:|-----:|----:|-----:|----:|
| wordcount |     67 |     62 |  74 |  107 |  86 |  107 | 421 |
| jsonapi   |    100 |     92 | 110 |  128 | 139 |  141 | 148 |
| primes    |    104 |    104 | 121 |  139 | 144 |  152 | 148 |
| **TOTAL** |**271** |    258 | 305 |  374 | 369 |  400 | 717 |
| vs AiLang |  1.00× |  0.95× |1.13×|1.38× |1.36×|1.48× |2.65×|

## Runtime (lower is better)

### wordcount

| lang   | mean    | vs C    |
|--------|--------:|--------:|
| C      |  32.1ms |   1.00× |
| Go     |  76.4ms |   2.38× |
| Rust   |  83.9ms |   2.62× |
| AiLang | 145.9ms |   4.55× |
| Java   | 188.1ms |   5.86× |
| Node   | 302.5ms |   9.42× |
| Python | 336.0ms |  10.47× |

### jsonapi

| lang   | mean    | vs C    |
|--------|--------:|--------:|
| Rust   |   2.6ms |   0.79× |
| Go     |   3.1ms |   0.94× |
| C      |   3.3ms |   1.00× |
| AiLang |   9.4ms |   2.85× |
| Node   |  18.6ms |   5.64× |
| Python |  19.6ms |   5.94× |
| Java   |  32.1ms |   9.73× |

### primes

| lang   | mean    | vs C    |
|--------|--------:|--------:|
| C      |   9.5ms |   1.00× |
| Go     |   9.6ms |   1.01× |
| Rust   |   9.9ms |   1.04× |
| AiLang |  15.4ms |   1.62× |
| Node   |  28.9ms |   3.04× |
| Java   |  33.7ms |   3.55× |
| Python | 362.6ms |  38.17× |
