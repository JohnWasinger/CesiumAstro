## About This Repository

This documents my hands-on experience with CCSDS packet processing from my time 
at LASP, expanded with C and Rust implementations relevant to embedded SDR systems.

# CesiumAstro

See [aboutCesiumAstro.md](aboutCesiumAstro.md) for background on CesiumAstro,
their technology, relevant CCSDS focus areas, and application advice.

---

Q> Show me an example in C on performing a CRC test on an array of numbers.
These numbers represent a data feed from a satellite. What other data checks are
usually performed on data feeds from space satellites.

A> Here's a C example performing a CRC-32 check on a simulated satellite data
feed:

[crc32_telemetry_intro.c](crc32_telemetry_intro.c)

- CRC-32 concepts, generic telemetry struct

**Example output from crc32_telemetry_intro.c Example output:**
=== Satellite Telemetry Packet ===
  Spacecraft ID : 0x1A2B
  Timestamp     : 1740000000
  Temperature   : -45.70 C
  Voltage       : 28.30 V
  Attitude      : Roll=0.12  Pitch=-1.05  Yaw=178.90
  Sequence #    : 42
  CRC-32        : 0x5F3A812C

[Test 1] Verifying clean packet...          PASS - CRC OK
[Test 2] Simulating bit-flip corruption...
  Verifying corrupted packet...             FAIL - Corruption detected!
[Test 3] After restoring data and recomputing CRC... PASS - CRC OK

**Other Data Integrity Checks Used in Real Satellite Feeds

Real satellite ground systems layer multiple checks on top of CRC.
Here's what's commonly used:

1. Forward Error Correction (FEC)

The most important layer. Because you cannot ask a satellite to resend data in
many missions, errors must be corrected, not just detected. Common schemes:

- Reed-Solomon — the workhorse of deep-space comms (used on Voyager, Mars
  rovers). Adds redundant symbols that let the receiver reconstruct corrupted
- bytes.
- Turbo Codes / LDPC — used on modern missions (e.g., Mars Reconnaissance
  Orbiter). Near Shannon-limit efficiency.
- Convolutional codes with Viterbi decoding — classic NASA standard.

1. Sequence Number / Frame Counter Checks

Every packet carries a monotonically increasing counter. The ground station
checks:

- Missing packets — gap in sequence numbers signals lost frames
- Duplicate packets — same sequence number received twice
- Out-of-order packets — reordering indicates routing problems

1. Checksum (Simple Additive)

Lighter-weight than CRC. Used inside sub-fields or as a secondary quick sanity
check (e.g., sum of all bytes mod 256 == 0).

1. Parity Bits

Used at the physical/bit layer. Single-bit parity detects odd-number bit errors
within a word. Often used in memory (EDAC — Error Detection and Correction) on
the spacecraft itself.

1. Range & Sanity Checks (Engineering Limit Checks)

Once the data is received and decoded, ground software validates that telemetry
values are physically plausible:

- Is battery voltage between 22–34V? (yellow/red limit alarms)
- Is temperature within operational bounds?
- Is attitude rate change physically possible given spacecraft inertia?

These are defined in a Limits Database (e.g., in NASA's ITOS, GOTS, or
commercial systems like SCOS-2000).

1. Timestamp Validation

- Packet time must be within an expected window of ground receive time
  (accounting for light travel delay)
- Monotonic — timestamps must not go backward
- Used to detect replay attacks or clock anomalies

1. Spacecraft Packet Standard Compliance (CCSDS)

Most space agencies follow the Consultative Committee for Space Data Systems
(CCSDS) standard. Packets have a defined header structure, and ground software
validates:

- Sync markers (0x1ACFFC1D for CCSDS frames)
- Version numbers
- Packet length field matches actual data length

Summary Table

| Check                  | Detects                    | Corrects? |
| ---                    | ---                        | ---       |
| CRC-32                 | Burst errors, corruption   | No        |
| Reed-Solomon / LDPC    | Bit errors                 | Yes       |
| Sequence numbers       | Lost / duplicate frames    | No        |
| Parity bits            | Single bit flips           | Limited   |
| Range/limit checks     | Sensor anomalies, glitches | No        |
| Timestamp validation   | Clock errors, replay       | No        |
| CCSDS structure check  | Malformed frames           | No        |

In practice, a mission like James Webb Space Telescope or Perseverance uses all
of these simultaneously in a layered defense approach.

Q> I've worked with processing data feeds that involved the CCSDS messages.

A> That's great hands-on experience! CCSDS is fascinating to work with — it's a
well-thought-out standard that balances reliability with the harsh realities of
space communication.

What kind of mission or system were you working with? Ground station software,
a simulator, or actual flight hardware processing? I'm curious what layer of
the stack you were dealing with — were you working at the Space Packet Protocol
level (APIDs, sequence flags, secondary headers), or deeper down at the
Transfer Frame level (Virtual Channels, Master Channels, OCFs)?

A few things that tend to trip people up when first working with CCSDS:

Packet fragmentation — the mismatch between Transfer Frame fixed sizes and
variable-length Space Packets means you constantly deal with packets spanning
frame boundaries. Reassembly logic can get messy fast, especially when virtual
channels are multiplexed together.

APID management — keeping track of which Application Process Identifier maps to
which subsystem (ADCS, power, payload, housekeeping) and making sure sequence
counts are tracked per-APID rather than globally.

Time formats — CCSDS has its own epoch (January 1, 1958!) and uses either CUC
(unsegmented) or CDS (day/millisecond segmented) time codes. Converting to Unix
time or UTC without subtle off-by-one errors is always fun.

Idle packets — knowing when to discard filler packets (APID 0x7FF) without
accidentally dropping real data.

Were you writing parsers from scratch, or working within an existing framework
like XTCE or a commercial ground system?

Q> How can I verify CCSDS mesages with a CRC test?

A> Great question! CCSDS uses a specific CRC variant — CRC-CCITT (CRC-16) — not
CRC-32. Here's a complete example:

[ccsds_crc.c](ccsds_crc.c)
— CRC-16/CCITT, proper CCSDS 133.0-B-2 structure

**Example output from [ccsds_crc.c](ccsds_crc.c):**
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

Key CCSDS-Specific Points Worth Noting

CRC scope matters — the standard specifies CRC covers the entire packet
including the primary header. Some implementations wrongly start after the
header, which causes interoperability failures between ground systems.

Big-endian on the wire — CCSDS is always big-endian (network byte order). On
x86 systems you must swap bytes when reading multi-byte fields, as shown with
`__builtin_bswap__` above. This is a very common source of bugs.

Packet Data Length field quirk — the CCSDS spec defines this field as (total
octets in the data field) - 1. It's an easy off-by-one trap, especially when
the data field includes the CRC itself.

APID 0x7FF is reserved — always filter idle/fill packets before CRC checking,
otherwise you'll generate false corruption alarms.

Per-APID sequence tracking — you'd normally maintain a table of last-seen
sequence counts keyed by APID to detect gaps independently per data stream.
Were you doing that kind of gap detection in your work?

---

See [lowLevelC.md](lowLevelC.md) for memory-mapped I/O patterns, the `volatile`
keyword, and how register access works in embedded systems (relevant to SDR
hardware interfacing).

---

Q> Would implementing this with Rust be more secure or am I just opening up a
new can of worms?

A> See [asRust.md](asRust.md) for a full discussion of the C to Rust transition,
including UB considerations, structural differences, and function signature
changes between ccsds_crc.c and ccsds_crc.rs.
