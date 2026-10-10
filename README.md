# P1 — Prime Search with Threads

Finds all prime numbers from 1 to **y** using **x** threads.

## Variants

| Folder | Printing | Work split |
|---|---|---|
| `variant1_immediate_range` | Print right away | Each thread gets a slice of the range |
| `variant2_deferred_range` | Print after all threads finish | Each thread gets a slice of the range |
| `variant3_immediate_divisibility` | Print right away | Threads share the divisor checks of each number |
| `variant4_deferred_divisibility` | Print after all threads finish | Threads share the divisor checks of each number |

## Config

Edit `config.txt` in the variant's folder:

```
threads=4
limit=1000
```

The file is checked before any thread starts. If anything is wrong, the program names the file and line, explains the problem, and exits with code 1.

- `threads` must be from 1 to 100000 and `limit` from 1 to 18446744073709551614.
- Values are whole numbers written with digits only: no signs, commas, spaces, decimals or exponents.
- Each key must appear exactly once, in any order. Keys are lowercase.
- `#` starts a comment, either on its own line or after a value. Blank lines are ignored.
- Save the file as UTF-8 (with or without a byte order mark) or ANSI. UTF-16 files are rejected.
- The config must be a regular file of at most 64 KiB.

## Build

You need a C++20 compiler (the divisibility variants use `<barrier>`).

Build one variant (run inside its folder):

```sh
g++ -std=c++20 -O2 -pthread main.cpp -o prime
```

On Windows (MSVC):

```
cl /std:c++20 /O2 /EHsc main.cpp /Fe:prime.exe
```

Or build all four with CMake (run in the project root):

```sh
cmake -S . -B build
cmake --build build
```

## Run

Run from inside the variant's folder so it finds `config.txt`:

```sh
./prime
```

To use a different config file: `./prime myconfig.txt`
