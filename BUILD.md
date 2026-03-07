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

**CRC-32 Telemetry Intro (generic satellite telemetry):**

```bash
./crc32_telemetry_intro
```

**Rust version:**

```bash
./ccsds_crc_rust
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

# CRC-32 telemetry intro
gcc -Wall -Wextra -std=c11 -O2 crc32_telemetry_intro.c -o crc32_telemetry_intro

./ccsds_crc
./crc32_telemetry_intro
```

**Rust version:**

```bash
rustc ccsds_crc.rs -o ccsds_crc_rust
./ccsds_crc_rust
```

Or with optimization:

```bash
rustc -C opt-level=3 ccsds_crc.rs -o ccsds_crc_rust
```

## Clean build

```bash
rm -rf build/
```
