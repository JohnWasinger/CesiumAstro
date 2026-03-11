#ifndef CCSDS_TABLES_H
#define CCSDS_TABLES_H

#include <stdint.h>
#include <stddef.h>

// ─────────────────────────────────────────────────────────────────────────────
// APID Mapping Table
// Maps Application Process Identifiers to subsystem names and packet types.
// Per CCSDS 133.0-B-2 — APID is 11 bits (0x000–0x7FE), 0x7FF = idle/fill
// ─────────────────────────────────────────────────────────────────────────────

typedef enum {
    PKT_TYPE_HOUSEKEEPING = 0,
    PKT_TYPE_SCIENCE,
    PKT_TYPE_ADCS,
    PKT_TYPE_POWER,
    PKT_TYPE_COMMS,
    PKT_TYPE_PAYLOAD,
    PKT_TYPE_UNKNOWN
} PacketType;

typedef struct {
    uint16_t    apid;
    PacketType  type;
    const char *subsystem;
    const char *description;
} ApidEntry;

// Static APID map — extend as mission grows
// In a real mission this would be loaded from an XTCE or JSON config
static const ApidEntry APID_TABLE[] = {
    { 0x001, PKT_TYPE_HOUSEKEEPING, "CDH",     "Command & Data Handling housekeeping"  },
    { 0x010, PKT_TYPE_POWER,        "EPS",     "Electrical Power System telemetry"     },
    { 0x020, PKT_TYPE_ADCS,         "ADCS",    "Attitude Determination & Control"      },
    { 0x030, PKT_TYPE_COMMS,        "RF",      "RF/SDR payload telemetry"              },
    { 0x040, PKT_TYPE_PAYLOAD,      "PAYLOAD", "Mission payload data"                  },
    { 0x050, PKT_TYPE_SCIENCE,      "SCI",     "Science instrument packets"            },
    { 0x100, PKT_TYPE_COMMS,        "XLINK",   "Inter-satellite crosslink telemetry"   },
    { 0x7FF, PKT_TYPE_UNKNOWN,      "IDLE",    "Idle/fill packet — discard"            },
};

#define APID_TABLE_SIZE (sizeof(APID_TABLE) / sizeof(APID_TABLE[0]))

// Lookup an APID entry — returns NULL if not found
static inline const ApidEntry *apid_lookup(uint16_t apid) {
    for (size_t i = 0; i < APID_TABLE_SIZE; i++) {
        if (APID_TABLE[i].apid == apid)
            return &APID_TABLE[i];
    }
    return NULL;
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-APID Sequence Tracking Table
// Tracks the last seen sequence count per APID to detect gaps and duplicates.
// Sequence count is 14-bit (0–16383), wraps at 0x3FFF per CCSDS spec.
// ─────────────────────────────────────────────────────────────────────────────

#define MAX_TRACKED_APIDS   32
#define SEQ_COUNT_MASK      0x3FFF   // 14-bit wraparound
#define SEQ_UNINITIALIZED   0xFFFF   // Sentinel — no packet seen yet

typedef struct {
    uint16_t apid;
    uint16_t last_seq;       // Last seen sequence count
    uint32_t total_received; // Total packets received on this APID
    uint32_t total_gaps;     // Total gap events detected
    uint32_t total_missing;  // Total missing packets across all gaps
} SeqTrackEntry;

typedef struct {
    SeqTrackEntry entries[MAX_TRACKED_APIDS];
    size_t        count;
} SeqTrackTable;

// Initialize the sequence tracking table
static inline void seq_track_init(SeqTrackTable *tbl) {
    tbl->count = 0;
    for (size_t i = 0; i < MAX_TRACKED_APIDS; i++) {
        tbl->entries[i].apid          = 0;
        tbl->entries[i].last_seq      = SEQ_UNINITIALIZED;
        tbl->entries[i].total_received = 0;
        tbl->entries[i].total_gaps    = 0;
        tbl->entries[i].total_missing = 0;
    }
}

// Find or create a tracking entry for an APID
// Returns NULL if table is full
static inline SeqTrackEntry *seq_track_get(SeqTrackTable *tbl, uint16_t apid) {
    // Search existing entries
    for (size_t i = 0; i < tbl->count; i++) {
        if (tbl->entries[i].apid == apid)
            return &tbl->entries[i];
    }
    // Create new entry if space available
    if (tbl->count < MAX_TRACKED_APIDS) {
        SeqTrackEntry *e = &tbl->entries[tbl->count++];
        e->apid           = apid;
        e->last_seq       = SEQ_UNINITIALIZED;
        e->total_received = 0;
        e->total_gaps     = 0;
        e->total_missing  = 0;
        return e;
    }
    return NULL;
}

// Update sequence tracking for a received packet.
// Returns number of missing packets (0 = no gap, >0 = gap detected)
static inline uint16_t seq_track_update(SeqTrackTable *tbl,
                                         uint16_t apid,
                                         uint16_t seq_count) {
    SeqTrackEntry *e = seq_track_get(tbl, apid);
    if (!e) return 0;

    e->total_received++;

    if (e->last_seq == SEQ_UNINITIALIZED) {
        e->last_seq = seq_count;
        return 0;  // First packet — no gap possible
    }

    uint16_t expected = (e->last_seq + 1) & SEQ_COUNT_MASK;
    uint16_t missing  = 0;

    if (seq_count != expected) {
        missing = (seq_count - expected) & SEQ_COUNT_MASK;
        e->total_gaps++;
        e->total_missing += missing;
    }

    e->last_seq = seq_count;
    return missing;
}

// ─────────────────────────────────────────────────────────────────────────────
// Engineering Limits Table
// Defines valid yellow and red alarm ranges for telemetry values per APID.
// Modeled after NASA ITOS / GOTS limit definitions.
// ─────────────────────────────────────────────────────────────────────────────

typedef enum {
    LIMIT_OK     = 0,  // Value within green band
    LIMIT_YELLOW = 1,  // Value outside yellow threshold — warning
    LIMIT_RED    = 2,  // Value outside red threshold — critical alarm
} LimitStatus;

typedef struct {
    uint16_t    apid;
    const char *param_name;
    const char *units;
    float       red_low;     // Below this → RED alarm
    float       yellow_low;  // Below this → YELLOW warning
    float       yellow_high; // Above this → YELLOW warning
    float       red_high;    // Above this → RED alarm
} LimitEntry;

// Engineering limits — values are illustrative for a LEO smallsat
// In a real mission these are loaded from a limits database (XML/JSON/XTCE)
static const LimitEntry LIMITS_TABLE[] = {
    // APID    Parameter          Units   RedLo   YlwLo  YlwHi   RedHi
    { 0x010, "bus_voltage",       "V",    22.0f,  24.0f,  32.0f,  34.0f },
    { 0x010, "battery_temp",      "C",   -20.0f, -10.0f,  40.0f,  50.0f },
    { 0x010, "solar_current",     "A",     0.0f,   0.1f,   5.0f,   6.0f },
    { 0x020, "attitude_rate_x",   "deg/s", -5.0f,  -3.0f,   3.0f,   5.0f },
    { 0x020, "attitude_rate_y",   "deg/s", -5.0f,  -3.0f,   3.0f,   5.0f },
    { 0x020, "attitude_rate_z",   "deg/s", -5.0f,  -3.0f,   3.0f,   5.0f },
    { 0x020, "reaction_wheel_rpm","RPM", -6000.f,-5000.f, 5000.f, 6000.f },
    { 0x030, "rf_temp",           "C",   -40.0f, -30.0f,  70.0f,  80.0f },
    { 0x030, "tx_power",          "dBm",  10.0f,  15.0f,  35.0f,  40.0f },
    { 0x001, "cpu_temp",          "C",   -20.0f, -10.0f,  70.0f,  80.0f },
    { 0x001, "cpu_load",          "%",     0.0f,   0.0f,  85.0f,  95.0f },
};

#define LIMITS_TABLE_SIZE (sizeof(LIMITS_TABLE) / sizeof(LIMITS_TABLE[0]))

// Check a telemetry value against its limit entry
static inline LimitStatus limit_check(const LimitEntry *lim, float value) {
    if (value <= lim->red_low  || value >= lim->red_high)   return LIMIT_RED;
    if (value <= lim->yellow_low || value >= lim->yellow_high) return LIMIT_YELLOW;
    return LIMIT_OK;
}

// Look up all limits for a given APID and parameter name
// Returns NULL if not found
static inline const LimitEntry *limit_lookup(uint16_t apid,
                                              const char *param_name) {
    for (size_t i = 0; i < LIMITS_TABLE_SIZE; i++) {
        if (LIMITS_TABLE[i].apid == apid &&
            strcmp(LIMITS_TABLE[i].param_name, param_name) == 0)
            return &LIMITS_TABLE[i];
    }
    return NULL;
}

static inline const char *limit_status_str(LimitStatus s) {
    switch (s) {
        case LIMIT_OK:     return "OK";
        case LIMIT_YELLOW: return "YELLOW";
        case LIMIT_RED:    return "RED";
        default:           return "UNKNOWN";
    }
}

#endif // CCSDS_TABLES_H
