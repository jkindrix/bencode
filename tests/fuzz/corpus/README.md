# Fuzz seed corpus

Each file here is one input to `LLVMFuzzerTestOneInput`. Both fuzz
harnesses (`fuzz_parse_sax`, `fuzz_parse_dom`) use this same corpus.

Seeds cover the obvious classes:

| File              | What it exercises                          |
|-------------------|--------------------------------------------|
| `01_empty_int`    | Empty input. Must fail with `TRUNCATED`.   |
| `02_zero`         | The integer `i0e`. Boundary canonical form. |
| `03_positive`     | A small positive integer.                  |
| `04_negative`     | A small negative integer.                  |
| `05_empty_string` | The empty byte string `0:`.                |
| `06_string`       | `4:spam` — basic non-empty string.         |
| `07_empty_list`   | `le` — empty list.                          |
| `08_empty_dict`   | `de` — empty dict.                          |
| `09_simple_dict`  | A dict with one entry.                     |
| `10_mixed_list`   | A list containing different value types.   |
| `11_nested`       | A nested dict + list.                      |
| `12_sorted_dict`  | A dict with multiple sorted keys.          |

Run locally:

```sh
cmake -S . -B build/fuzz -DBENCODE_BUILD_FUZZERS=ON -DCMAKE_C_COMPILER=clang
cmake --build build/fuzz --target fuzz_parse_sax fuzz_parse_dom
./build/fuzz/tests/fuzz/fuzz_parse_sax tests/fuzz/corpus \
    -dict=tests/fuzz/bencode.dict -max_total_time=60
./build/fuzz/tests/fuzz/fuzz_parse_dom tests/fuzz/corpus \
    -dict=tests/fuzz/bencode.dict -max_total_time=60
```

To merge new finds back into the corpus:

```sh
./build/fuzz/tests/fuzz/fuzz_parse_sax -merge=1 tests/fuzz/corpus path/to/new/inputs
```
