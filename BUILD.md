# Build Instructions

## Prerequisites

- CMake 3.10 or higher
- GCC or Clang C compiler
- Make (or Ninja)

## Building with CMake

### Standard build

```bash
mkdir build
cd build
cmake ..
make
```

### Debug build

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

### Release build (optimized)

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

## Running the examples

After building, executables will be in the `build/` directory:

**CCSDS CRC-16 (proper CCSDS 133.0-B-2 implementation):**

```bash
./ccsds_crc
```

Expected output:

```text
=== CCSDS Telemetry Packet ===
  APID            : 0x123 (291)
  Sequence Count  : 100
  Sequence Flags  : 0x3 (standalone)
  User Data Len   : 23 bytes
  Coarse Time     : 3822844800
  Fine Time       : 1234
  CRC-16 (wire)   : 0x84F2

[Test 1] Verify clean packet          : PASS - CRC OK
[Test 2] Flip a bit in payload         : FAIL - Corruption detected!
[Test 3] Restored and recomputed CRC   : PASS - CRC OK
[Test 4] Corrupt primary header APID   : FAIL - Header corruption detected!
```

**CCSDS Runtime Tables (APID map, sequence gap detection, engineering limits):**

```bash
./ccsds_tables_demo
```

Expected output:

```text
Loaded 8 APIDs and 11 limits from mission_config.json

--- APID Table Lookup ---
  APID 0x001 → [CDH     ] Command & Data Handling housekeeping
  APID 0x020 → [ADCS    ] Attitude Determination & Control
  APID 0x030 → [RF      ] RF/SDR payload telemetry
  APID 0x100 → [XLINK   ] Inter-satellite crosslink telemetry
  APID 0x7FF → [IDLE    ] Idle/fill packet — discard
  APID 0x999 → NOT FOUND

--- Sequence Gap Detection ---
  APID 0x020 seq   0 — OK
  APID 0x020 seq   4 — GAP: 1 packet(s) missing
  APID 0x020 seq   9 — GAP: 3 packet(s) missing

--- Engineering Limits Check ---
  EPS bus_voltage          =   28.30 V      → OK
  EPS bus_voltage          =   23.50 V      → YELLOW
  EPS bus_voltage          =   21.00 V      → RED
```

> Note: `mission_config.json` must be present in the working directory when running `ccsds_tables_demo`.

**Rust version:**

```bash
./ccsds_crc_rust
```

Or build and run directly via Cargo without CMake:

```bash
cargo run
```

## Installing

```bash
sudo make install
```

This installs binaries to `/usr/local/bin` by default.

To change the install prefix:

```bash
cmake -DCMAKE_INSTALL_PREFIX=/custom/path ..
make
make install
```

## Direct compilation without CMake

If you prefer to compile directly:

**C implementations:**

```bash
# CCSDS CRC-16
gcc -Wall -Wextra -std=c11 -O2 ccsds_crc.c -o ccsds_crc

# CCSDS runtime tables demo (mission_config.json must be in current directory)
gcc -Wall -Wextra -std=c11 -O2 ccsds_tables_demo.c -o ccsds_tables_demo

./ccsds_crc
./ccsds_tables_demo
```

**Rust version (direct rustc):**

```bash
rustc ccsds_crc.rs -o ccsds_crc_rust
./ccsds_crc_rust
```

Or with optimization:

```bash
rustc -C opt-level=3 ccsds_crc.rs -o ccsds_crc_rust
```

**Rust version (Cargo):**

A `Cargo.toml` is included in the repo. To build and run via Cargo:

```bash
cargo build
cargo run
```

Or optimized:

```bash
cargo build --release
./target/release/ccsds_crc
```

The `Cargo.toml` currently has no active dependencies — the implementation uses
only the standard library. To switch to a production CCSDS crate, uncomment the
relevant dependency in `Cargo.toml` and update the `[[bin]]` path to point at
your new source file. See [asRust.md](asRust.md) for the available crates.

## Clean build

```bash
rm -rf build/
```
