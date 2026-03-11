#ifndef CCSDS_TABLES_H
#define CCSDS_TABLES_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// ─────────────────────────────────────────────────────────────────────────────
// ccsds_tables.h
//
// Type definitions and logic functions for CCSDS packet processing tables.
// NO hardcoded mission data lives here — all table contents are loaded at
// runtime from mission_config.json (or equivalent config source).
//
// Per CCSDS 133.0-B-2
// ─────────────────────────────────────────────────────────────────────────────


// ─────────────────────────────────────────────────────────────────────────────
// APID Mapping Table
// Maps Application Process Identifiers to subsystem names and packet types.
// APID is 11 bits (0x000–0x7FE). 0x7FF = idle/fill, always discard.
// ─────────────────────────────────────────────────────────────────────────────

typedef enum {
    PKT_TYPE_HOUSEKEEPING = 0,
    PKT_TYPE_SCIENCE,
    PKT_TYPE_ADCS,
    PKT_TYPE_POWER,
    PKT_TYPE_COMMS,
    PKT_TYPE_PAYLOAD,
    PKT_TYPE_IDLE,
    PKT_TYPE_UNKNOWN
} PacketType;

typedef struct {
    uint16_t   apid;
    PacketType type;
    char       subsystem[16];
    char       description[64];
} ApidEntry;

// Look up an APID entry in a runtime-loaded table.
// Returns NULL if not found.
static inline const ApidEntry *apid_lookup(const ApidEntry *table,
                                            size_t           count,
                                            uint16_t         apid) {
    for (size_t i = 0; i < count; i++) {
        if (table[i].apid == apid)
            return &table[i];
    }
    return NULL;
}


// ─────────────────────────────────────────────────────────────────────────────
// Per-APID Sequence Tracking Table
// Tracks the last seen sequence count per APID to detect gaps and duplicates.
// Sequence count is 14-bit (0–16383), wraps at 0x3FFF per CCSDS spec.
// ─────────────────────────────────────────────────────────────────────────────

#define MAX_TRACKED_APIDS  32
#define SEQ_COUNT_MASK     0x3FFF  // 14-bit wraparound
#define SEQ_UNINITIALIZED  0xFFFF  // Sentinel — no packet seen yet

typedef struct {
    uint16_t apid;
    uint16_t last_seq;        // Last seen sequence count
    uint32_t total_received;  // Total packets received on this APID
    uint32_t total_gaps;      // Total gap events detected
    uint32_t total_missing;   // Total missing packets across all gaps
} SeqTrackEntry;

typedef struct {
    SeqTrackEntry entries[MAX_TRACKED_APIDS];
    size_t        count;
} SeqTrackTable;

// Initialize all entries to uninitialized state
static inline void seq_track_init(SeqTrackTable *tbl) {
    tbl->count = 0;
    for (size_t i = 0; i < MAX_TRACKED_APIDS; i++) {
        tbl->entries[i].apid           = 0;
        tbl->entries[i].last_seq       = SEQ_UNINITIALIZED;
        tbl->entries[i].total_received = 0;
        tbl->entries[i].total_gaps     = 0;
        tbl->entries[i].total_missing  = 0;
    }
}

// Find or create a tracking entry for an APID.
// Returns NULL if table is full.
static inline SeqTrackEntry *seq_track_get(SeqTrackTable *tbl, uint16_t apid) {
    for (size_t i = 0; i < tbl->count; i++) {
        if (tbl->entries[i].apid == apid)
            return &tbl->entries[i];
    }
    if (tbl->count < MAX_TRACKED_APIDS) {
        SeqTrackEntry *e  = &tbl->entries[tbl->count++];
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
// Returns number of missing packets (0 = no gap, >0 = gap detected).
static inline uint16_t seq_track_update(SeqTrackTable *tbl,
                                         uint16_t       apid,
                                         uint16_t       seq_count) {
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
    uint16_t apid;
    char     param_name[32];
    char     units[8];
    float    red_low;     // Below this → RED alarm
    float    yellow_low;  // Below this → YELLOW warning
    float    yellow_high; // Above this → YELLOW warning
    float    red_high;    // Above this → RED alarm
} LimitEntry;

// Check a telemetry value against its limit entry.
static inline LimitStatus limit_check(const LimitEntry *lim, float value) {
    if (value <= lim->red_low    || value >= lim->red_high)    return LIMIT_RED;
    if (value <= lim->yellow_low || value >= lim->yellow_high) return LIMIT_YELLOW;
    return LIMIT_OK;
}

// Look up a limit entry in a runtime-loaded table by APID and parameter name.
// Returns NULL if not found.
static inline const LimitEntry *limit_lookup(const LimitEntry *table,
                                              size_t            count,
                                              uint16_t          apid,
                                              const char       *param_name) {
    for (size_t i = 0; i < count; i++) {
        if (table[i].apid == apid &&
            strcmp(table[i].param_name, param_name) == 0)
            return &table[i];
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
