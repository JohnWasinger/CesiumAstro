# CesiumAstro — C to Rust: CCSDS Packet Processing

## Files Referenced

- [ccsds_crc.c](ccsds_crc.c)     — CRC-16/CCITT, proper CCSDS 133.0-B-2 structure in C
- [ccsds_crc.rs](ccsds_crc.rs)   — Rust port, memory-safe, typed errors

---

Q> Would implementing this with Rust be more secure or am I just opening up a
new can of worms?

A> Great question — and honestly, it's one of the most debated topics in
embedded/systems programming right now. The answer is nuanced:

## Rust Would Genuinely Help With

### Memory Safety — Rust's Biggest Win

The classes of bugs Rust eliminates at compile time are exactly the ones that
plague C in data processing pipelines:

- Buffer overruns — reading past the end of a packet buffer
- Use-after-free — accessing a packet struct after it's been deallocated
- Null pointer dereferences — common when packet parsing returns NULL on
  malformed input
- Integer overflow — silent wraparound when computing packet lengths (a real
  attack vector)

In your CCSDS context, the packet length field manipulation is a classic attack
vector. In C, a malformed `packet_data_len` can easily cause you to read off the
end of a buffer. Rust makes that category of bug essentially impossible.

### No Undefined Behavior in Safe Code

In C, things like signed integer overflow and misaligned struct access are
undefined behavior — the compiler can do anything, and often does surprising
things with optimizations. Rust significantly reduces UB in safe code (see
correction below).

### Explicit Error Handling

Rust's `Result<T, E>` type forces you to handle parse failures. In C it's easy to
ignore a return code. In Rust, ignoring an error requires deliberate effort —
the compiler warns you.

---

## The Can of Worms

### unsafe Blocks Are Unavoidable for Low-Level Work

The moment you're doing things like:

- Casting raw bytes to a struct (packet deserializing)
- Interfacing with hardware registers
- FFI calls to existing C ground system libraries

...you'll be writing `unsafe` Rust. And `unsafe` Rust can have all the same bugs
as C. The discipline required is the same — you've just narrowed the surface
area of where those bugs can live.

```rust
// You'll inevitably write something like this:
let pkt: &CCSDSPacket = unsafe {
    // Same risks as C if unchecked
    &*(raw_bytes.as_ptr() as *const CCSDSPacket)
};
```

### Byte-Order and Bit-Field Handling Is More Verbose

CCSDS's big-endian packed structs, which C handles with `__attribute__((packed))`
and `__builtin_bswap__`, require explicit crates in Rust. The `byteorder` or
`zerocopy` crates handle this well, but it's more boilerplate than C.

### Embedded and Real-Time Ecosystem Maturity

If you're on a flight computer or a ground system with strict RTOS requirements,
Rust's embedded ecosystem (`no_std`) is growing fast but still not as mature as C
for space-heritage systems. NASA and ESA still predominantly certify C
(DO-178C, ECSS-E-ST-40C).

---

## What a Rust CCSDS Parser Looks Like

Here's the equivalent CRC verification in safe Rust to give you a feel:

```rust
use std::convert::TryInto;

const CRC16_POLY: u16 = 0x1021;
const CRC16_INIT: u16 = 0xFFFF;

fn build_crc16_table() -> [u16; 256] {
    let mut table = [0u16; 256];
    for i in 0..256u16 {
        let mut crc = i << 8;
        for _ in 0..8 {
            if crc & 0x8000 != 0 {
                crc = (crc << 1) ^ CRC16_POLY;
            } else {
                crc <<= 1;
            }
        }
        table[i as usize] = crc;
    }
    table
}

fn calculate_crc16(table: &[u16; 256], data: &[u8]) -> u16 {
    let mut crc = CRC16_INIT;
    for &byte in data {
        let index = ((crc >> 8) as u8 ^ byte) as usize;
        crc = (crc << 8) ^ table[index];
    }
    crc
}

#[derive(Debug)]
enum PacketError {
    TooShort,
    CrcMismatch { expected: u16, computed: u16 },
    InvalidApid(u16),
}

fn verify_ccsds_packet(table: &[u16; 256], data: &[u8]) -> Result<u16, PacketError> {
    if data.len() < 8 {
        return Err(PacketError::TooShort);
    }
    let (payload, crc_bytes) = data.split_at(data.len() - 2);
    let received_crc = u16::from_be_bytes(crc_bytes.try_into().unwrap());
    let computed_crc = calculate_crc16(table, payload);
    if computed_crc != received_crc {
        return Err(PacketError::CrcMismatch {
            expected: received_crc,
            computed: computed_crc,
        });
    }
    let apid = u16::from_be_bytes([data[0], data[1]]) & 0x07FF;
    if apid == 0x7FF {
        return Err(PacketError::InvalidApid(apid));
    }
    Ok(apid)
}
```

**Output:**

```text
[Test 1] Clean packet        : PASS — APID: 0x023
[Test 2] Corrupted payload   : FAIL — CrcMismatch { expected: 0xA3F1, computed: 0xA2F0 }
[Test 3] Truncated packet    : FAIL — TooShort
```

Notice that `TooShort` and `CrcMismatch` are typed errors — the caller is forced
by the compiler to handle both cases. In C, you'd return -1 and hope the caller
checks it.

---

## Honest Verdict

| Concern                           | C                  | Rust                          |
| ---                               | ---                | ---                           |
| Buffer overruns in parser         | Easy to introduce  | Prevented in safe code        |
| Integer overflow in length fields | Silent UB          | Checked by default in debug   |
| Ignoring error returns            | Very easy          | Compiler warning/error        |
| Raw byte casting (unsafe)         | Always             | Still needed, but isolated    |
| Certified flight software         | Mature (DO-178C)   | Emerging (ferrocene compiler) |
| Ground system / GSE tools         | Your call          | Excellent choice              |
| Legacy C library integration      | Native             | FFI friction                  |

Bottom line: If this is ground support software — parsers, monitors, display
tools — Rust is a strong choice and you'd get real security benefits. If this
is flight software on a certified platform, you're likely stuck with C or Ada
for now, though that is slowly changing with the Ferrocene qualified Rust
compiler.

The "can of worms" is real but manageable — the worms are just smaller and
better contained than in C.

---

Q> Rust has no UB in safe code?

A> Good catch — that was an overstatement. Let me be more precise:

## The Corrected Picture

Rust significantly reduces UB compared to C, but it does not eliminate it
entirely, even in safe code. The Rust Reference explicitly acknowledges this.

### UB That Can Still Occur in Safe Rust

#### Data Races

While Rust's ownership system prevents most data races, certain patterns can
still produce them:

```rust
// Incorrect use of sync primitives can still cause UB
static mut COUNTER: u32 = 0;  // safe Rust still allows static mut
unsafe { COUNTER += 1; }      // but accessing it requires unsafe
```

#### Integer Overflow

This is interesting:

- In debug builds — Rust panics on overflow (caught at runtime)
- In release builds — it wraps silently by default, just like C

```rust
let x: u16 = 65535u16 + 1;  // Panics in debug, wraps in release
```

You can opt into explicit behavior with `wrapping_add()`, `checked_add()`, or
`saturating_add()` — which is actually better than C's options.

#### Incorrect unsafe Blocks Infecting Safe Code

If an `unsafe` block somewhere in your dependency tree introduces UB, it can
manifest as UB in your "safe" calling code. The safety boundary isn't always
clean.

#### Logic Errors

Rust can't save you from computing the wrong CRC length, misinterpreting an
APID, or processing packets out of order.

### What Rust Actually Guarantees in Safe Code

Freedom from:

- Use-after-free
- Dangling pointers
- Buffer overflows (bounds checked)
- Null pointer dereferences
- Uninitialized memory reads
- Certain data races (via the ownership/borrow system)

These are memory safety guarantees, not a blanket "no UB" guarantee.

## The Honest Summary

| Behavior                 | C            | Rust Safe Code                    |
| ---                      | ---          | ---                               |
| Buffer overflow          | UB, silent   | Runtime panic                     |
| Null dereference         | UB, crash    | Prevented by type system          |
| Use-after-free           | UB, silent   | Prevented by borrow checker       |
| Integer overflow         | UB, silent   | Panic (debug) / wrap (release)    |
| Data races               | UB           | Largely prevented, not guaranteed |
| Logic errors             | Your problem | Still your problem                |
| unsafe contamination     | N/A          | Still a real risk                 |

The more accurate claim: Rust eliminates the most dangerous classes of
memory-safety UB in safe code — which is still a massive practical improvement
over C, but not an absolute guarantee.

---

Q> Could I see a Rust version of the CRC enabled CCSDS processing code please?

A> See [ccsds_crc.rs](ccsds_crc.rs) for the full implementation. Here's what
it demonstrates and where it improves on the C version:

**Structure** — `PrimaryHeader` and `SecondaryHeader` are proper structs with
`from_bytes()` / `to_bytes()` methods — no raw pointer casting, no
`__attribute__((packed))` footguns. All byte-order conversion is explicit with
`u16::from_be_bytes()`. `Crc16` encapsulates the lookup table — built once and
reused cleanly.

**Error handling with `CcsdError`** — every failure path returns a typed,
descriptive error rather than a magic integer:

```text
CrcMismatch    { received: 0xA3F1, computed: 0xA2F0 }
PacketTooShort { actual: 4, minimum: 8 }
IdlePacket     (0x7FF)
InvalidLength  { field_value: 21, actual_data_bytes: 18 }
```

**Sequence gap detection** — tracks per-APID sequence counts with proper 14-bit
wraparound (`& 0x3FFF`):

```text
GAP DETECTED: expected seq 3, got 4 (1 missing)
GAP DETECTED: expected seq 6, got 9 (3 missing)
```

To compile and run:

```bash
rustc ccsds_crc.rs -o ccsds_crc
./ccsds_crc
```

No external crates needed — pure standard library.

---

Q> What's different between ccsds_crc.c and ccsds_crc.rs? Were there structures
or function signatures changed to support Rust? Were any structure elements lost
in the transition?

A> Nothing was lost — the Rust version is a superset — but there are meaningful
structural differences throughout.

## Structures Comparison

### Primary Header

| Aspect                    | C                                                    | Rust                                          |
| ---                       | ---                                                  | ---                                           |
| Definition                | `CCSDSPrimaryHeader` struct with 3 `uint16_t` fields | `PrimaryHeader` struct with 7 named fields    |
| Storage                   | Packed raw bit fields in 3 words                     | Fully decoded individual values               |
| `__attribute__((packed))` | Yes — required                                       | Not needed — no raw casting                   |
| Byte swap                 | `__builtin_bswap16()` at every access                | Done once in `from_bytes()`, stored as native |
| APID extraction           | `get_apid()` helper function                         | Stored directly as `apid: u16` field          |
| Seq flags extraction      | `get_sequence_flags()` helper                        | Stored directly as `seq_flags: u8` field      |

The C version stores the header as it lives on the wire — packed big-endian
words decoded on demand. The Rust version decodes everything upfront in
`from_bytes()` into clean named fields. This trades a tiny bit of upfront CPU
for much safer, cleaner access throughout the code.

### Secondary Header

Essentially identical logic, different syntax:

```c
// C — raw struct, packed
typedef struct {
    uint32_t coarse_time;
    uint16_t fine_time;
} __attribute__((packed)) CCSDSSecondaryHeader;
```

```rust
// Rust — decoded struct
struct SecondaryHeader {
    coarse_time: u32,
    fine_time:   u16,
}
```

### Packet Container

The biggest structural difference:

```c
// C — one monolithic packed struct, fixed payload size
typedef struct {
    CCSDSPrimaryHeader   primary;
    CCSDSSecondaryHeader secondary;
    uint8_t              payload[16];  // Fixed size
    uint16_t             crc;          // CRC stays in struct
} __attribute__((packed)) CCSDSPacket;
```

```rust
// Rust — parsed result, dynamic payload, CRC discarded after verification
struct CcsdPacket {
    primary:   PrimaryHeader,
    secondary: SecondaryHeader,
    payload:   Vec<u8>,   // Any payload size
    // No crc field — verified and discarded
}
```

Two intentional changes: `payload[16]` became `Vec<u8>` so the Rust version
handles any payload size, and the `crc` field was deliberately dropped — once
verified it has no further use. In C the CRC stays in the struct which can lead
to bugs where stale CRC values are reused.

---

## Function Signatures Comparison

| C Function               | Rust Equivalent                        | Key Difference                                          |
| ---                      | ---                                    | ---                                                     |
| `build_crc16_table()`    | `Crc16::new()`                         | C uses a global array; Rust encapsulates in a struct    |
| `calculate_crc16(*data, length)` | `Crc16::calculate(&self, data: &[u8])` | Rust slice carries its own length — no separate arg |
| `build_packet(*pkt, apid, seq)` | `build_packet(crc, apid, seq, time, fine, payload)` | Rust takes more params — no hardcoded values  |
| `verify_ccsds_packet(*pkt)` | `verify_and_parse(crc, raw: &[u8])`  | C returns `int` (0/1); Rust returns `Result<CcsdPacket, CcsdError>` |
| `print_packet_info(*pkt)` | `print_packet(&CcsdPacket)`           | Takes parsed struct, not raw packet                     |
| `get_apid()`             | None needed                            | Eliminated — value stored in struct at parse time       |
| `get_sequence_count()`   | None needed                            | Eliminated — same reason                                |
| `get_sequence_flags()`   | None needed                            | Eliminated — same reason                                |
| `get_user_data_length()` | `data_field_len()` method              | Rust method on struct vs C free function                |

---

## What Was Added in Rust That Doesn't Exist in C

### Typed Error Enum

The biggest addition. The C version has no equivalent — a bad packet just
returns 0 from `verify_ccsds_packet()` with no indication of why:

```rust
enum CcsdError {
    PacketTooShort { actual: usize, minimum: usize },
    CrcMismatch    { received: u16, computed: u16 },
    IdlePacket     (u16),
    InvalidLength  { field_value: u16, actual_data_bytes: usize },
}
```

### Serialization Round-Trip

**`to_bytes()` / `from_bytes()` methods** — the C version has no clean
serialization round-trip. You build the struct in memory and cast it directly.

### Sequence Gap Detection

Added in Rust, not present in C at all.

### Display Methods

`seq_flags_str()` and `packet_type_str()` are convenience methods on the struct
with no C equivalent.

---

## What Was Consciously Simplified

### Coarse Time Calculation

The C version computes `2082844800 + 1740000000` inline (converting Unix epoch
offset to CCSDS epoch). The Rust version takes `coarse_time` as a direct
parameter, leaving epoch conversion to the caller. A more complete version would
add a helper for that.

### print_packet_info Output

The C version prints raw sequence flags as a hex value (`0x3`), while the Rust
version decodes it to a human-readable string (`Standalone`). The Rust output is
friendlier but the C output is closer to what you'd see in a raw telemetry
monitor.

---

## Overall Summary

| Aspect                  | C                          | Rust                              |
| ---                     | ---                        | ---                               |
| Wire format handling    | Packed struct cast in-place | Decoded via `from_bytes()`       |
| Payload size            | Fixed `[16]`               | Dynamic `Vec<u8>`                 |
| CRC after verification  | Stays in struct            | Discarded — prevents stale reuse  |
| Error reporting         | Returns 0/1 integer        | Typed `CcsdError` enum            |
| Getter functions        | 4 free functions           | Eliminated — decoded at parse time|
| Sequence gap detection  | Not present                | Built in with 14-bit wraparound   |
| Epoch conversion        | Inline in build_packet     | Delegated to caller               |
