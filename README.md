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

## Build

You need a C++17 compiler.

Build one variant (run inside its folder):

```sh
g++ -std=c++17 -O2 -pthread main.cpp -o prime
```

On Windows (MSVC):

```
cl /std:c++17 /O2 /EHsc main.cpp /Fe:prime.exe
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
