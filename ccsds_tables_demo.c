#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ccsds_tables.h"

// ─────────────────────────────────────────────────────────────────────────────
// Demo 1 — Static header tables (ccsds_tables.h)
// This is what you'd use in flight software or a simple ground tool where
// the configuration is fixed at compile time.
// ─────────────────────────────────────────────────────────────────────────────

void demo_static_tables(void) {
    printf("=== Demo 1: Static Header Tables ===\n\n");

    // ── APID lookup ──────────────────────────────────────────────────────────
    printf("--- APID Table Lookup ---\n");
    uint16_t test_apids[] = { 0x001, 0x020, 0x030, 0x100, 0x7FF, 0x999 };
    for (size_t i = 0; i < sizeof(test_apids)/sizeof(test_apids[0]); i++) {
        uint16_t apid = test_apids[i];
        const ApidEntry *e = apid_lookup(apid);
        if (e)
            printf("  APID 0x%03X → [%-8s] %s\n", apid, e->subsystem, e->description);
        else
            printf("  APID 0x%03X → NOT FOUND\n", apid);
    }

    // ── Sequence tracking ────────────────────────────────────────────────────
    printf("\n--- Sequence Gap Detection ---\n");
    SeqTrackTable seq_tbl;
    seq_track_init(&seq_tbl);

    // Simulate a stream with gaps on APID 0x020 (ADCS)
    uint16_t adcs_stream[] = { 0, 1, 2, 4, 5, 9, 10, 11 };
    for (size_t i = 0; i < sizeof(adcs_stream)/sizeof(adcs_stream[0]); i++) {
        uint16_t seq = adcs_stream[i];
        uint16_t missing = seq_track_update(&seq_tbl, 0x020, seq);
        if (missing > 0)
            printf("  APID 0x020 seq %3u — GAP: %u packet(s) missing\n",
                   seq, missing);
        else
            printf("  APID 0x020 seq %3u — OK\n", seq);
    }

    // Print summary stats
    SeqTrackEntry *e = seq_track_get(&seq_tbl, 0x020);
    if (e) {
        printf("\n  APID 0x020 Summary:\n");
        printf("    Total received : %u\n", e->total_received);
        printf("    Gap events     : %u\n", e->total_gaps);
        printf("    Total missing  : %u\n", e->total_missing);
    }

    // ── Engineering limits ───────────────────────────────────────────────────
    printf("\n--- Engineering Limits Check ---\n");

    // Simulate some EPS telemetry values — normal, warning, and alarm
    struct { const char *param; float value; } eps_values[] = {
        { "bus_voltage",   28.3f  },   // Normal
        { "bus_voltage",   23.5f  },   // Yellow — low
        { "bus_voltage",   21.0f  },   // Red — critically low
        { "battery_temp",  25.0f  },   // Normal
        { "battery_temp",  47.0f  },   // Yellow — high
        { "solar_current",  0.05f },   // Yellow — low current
    };

    for (size_t i = 0; i < sizeof(eps_values)/sizeof(eps_values[0]); i++) {
        const LimitEntry *lim = limit_lookup(0x010, eps_values[i].param);
        if (lim) {
            LimitStatus s = limit_check(lim, eps_values[i].value);
            printf("  EPS %-20s = %7.2f %-5s → %s\n",
                   eps_values[i].param,
                   eps_values[i].value,
                   lim->units,
                   limit_status_str(s));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 2 — Minimal JSON parser
//
// In production you'd use a library like cJSON or jsmn. This is a minimal
// hand-rolled parser just to show the concept without external dependencies.
//
// The key point: the same structs (ApidEntry, LimitEntry) are populated
// whether data comes from the static header or a JSON file — the rest of
// the ground software doesn't care which source was used.
// ─────────────────────────────────────────────────────────────────────────────

// Runtime-allocated versions of the tables (loaded from JSON)
#define MAX_RUNTIME_APIDS   32
#define MAX_RUNTIME_LIMITS  64

typedef struct {
    uint16_t apid;
    char     subsystem[16];
    char     description[64];
} RuntimeApidEntry;

typedef struct {
    uint16_t apid;
    char     param_name[32];
    char     units[8];
    float    red_low;
    float    yellow_low;
    float    yellow_high;
    float    red_high;
} RuntimeLimitEntry;

typedef struct {
    RuntimeApidEntry  apids[MAX_RUNTIME_APIDS];
    size_t            apid_count;
    RuntimeLimitEntry limits[MAX_RUNTIME_LIMITS];
    size_t            limit_count;
} MissionConfig;

// Minimal JSON field extractor — finds "key": value in a JSON string.
// Not a full parser — just enough for flat key/value pairs.
static int json_get_string(const char *json, const char *key,
                            char *out, size_t out_len) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ' || *p == ':' || *p == ' ') p++;
    if (*p == '"') {
        p++;
        size_t i = 0;
        while (*p && *p != '"' && i < out_len - 1)
            out[i++] = *p++;
        out[i] = '\0';
        return 1;
    }
    return 0;
}

static int json_get_float(const char *json, const char *key, float *out) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ' || *p == ':' || *p == ' ') p++;
    *out = (float)atof(p);
    return 1;
}

static uint16_t parse_hex(const char *s) {
    return (uint16_t)strtol(s, NULL, 16);
}

// Load mission config from a JSON file
// Returns 1 on success, 0 on failure
int load_mission_config(const char *filename, MissionConfig *cfg) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Cannot open config: %s\n", filename);
        return 0;
    }

    // Read entire file into buffer
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buf = malloc(size + 1);
    if (!buf) { fclose(f); return 0; }
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);

    cfg->apid_count  = 0;
    cfg->limit_count = 0;

    // Parse apid_table entries — find each { block inside "apid_table"
    const char *apid_section = strstr(buf, "\"apid_table\"");
    if (apid_section) {
        const char *p = strchr(apid_section, '[');
        while (p && cfg->apid_count < MAX_RUNTIME_APIDS) {
            const char *entry_start = strchr(p, '{');
            const char *entry_end   = strchr(p, '}');
            if (!entry_start || !entry_end) break;

            // Extract just this entry
            size_t entry_len = entry_end - entry_start + 1;
            char entry[256] = {0};
            if (entry_len < sizeof(entry)) {
                strncpy(entry, entry_start, entry_len);
                RuntimeApidEntry *e = &cfg->apids[cfg->apid_count];
                char apid_str[16] = {0};
                if (json_get_string(entry, "apid",       apid_str,      sizeof(apid_str)) &&
                    json_get_string(entry, "subsystem",  e->subsystem,  sizeof(e->subsystem)) &&
                    json_get_string(entry, "description",e->description,sizeof(e->description))) {
                    e->apid = parse_hex(apid_str);
                    cfg->apid_count++;
                }
            }
            p = entry_end + 1;
        }
    }

    // Parse limits_table entries
    const char *limits_section = strstr(buf, "\"limits_table\"");
    if (limits_section) {
        const char *p = strchr(limits_section, '[');
        while (p && cfg->limit_count < MAX_RUNTIME_LIMITS) {
            const char *entry_start = strchr(p, '{');
            const char *entry_end   = strchr(p, '}');
            if (!entry_start || !entry_end) break;

            size_t entry_len = entry_end - entry_start + 1;
            char entry[256] = {0};
            if (entry_len < sizeof(entry)) {
                strncpy(entry, entry_start, entry_len);
                RuntimeLimitEntry *e = &cfg->limits[cfg->limit_count];
                char apid_str[16] = {0};
                if (json_get_string(entry, "apid",  apid_str,    sizeof(apid_str)) &&
                    json_get_string(entry, "param", e->param_name,sizeof(e->param_name)) &&
                    json_get_string(entry, "units", e->units,    sizeof(e->units)) &&
                    json_get_float (entry, "red_low",    &e->red_low)    &&
                    json_get_float (entry, "yellow_low", &e->yellow_low) &&
                    json_get_float (entry, "yellow_high",&e->yellow_high)&&
                    json_get_float (entry, "red_high",   &e->red_high)) {
                    e->apid = parse_hex(apid_str);
                    cfg->limit_count++;
                }
            }
            p = entry_end + 1;
        }
    }

    free(buf);
    printf("Loaded %zu APIDs and %zu limits from %s\n",
           cfg->apid_count, cfg->limit_count, filename);
    return 1;
}

void demo_json_tables(const char *config_file) {
    printf("\n=== Demo 2: JSON-Loaded Tables (%s) ===\n\n", config_file);

    MissionConfig cfg;
    if (!load_mission_config(config_file, &cfg))
        return;

    // Print loaded APID table
    printf("\n--- Loaded APID Table ---\n");
    for (size_t i = 0; i < cfg.apid_count; i++) {
        printf("  APID 0x%03X → [%-8s] %s\n",
               cfg.apids[i].apid,
               cfg.apids[i].subsystem,
               cfg.apids[i].description);
    }

    // Print loaded limits table
    printf("\n--- Loaded Limits Table ---\n");
    printf("  %-6s  %-22s  %-6s  %8s  %8s  %8s  %8s\n",
           "APID", "Parameter", "Units",
           "RedLow", "YlwLow", "YlwHigh", "RedHigh");
    printf("  %-6s  %-22s  %-6s  %8s  %8s  %8s  %8s\n",
           "------", "----------------------", "------",
           "-------", "-------", "-------", "-------");
    for (size_t i = 0; i < cfg.limit_count; i++) {
        RuntimeLimitEntry *e = &cfg.limits[i];
        printf("  0x%03X  %-22s  %-6s  %8.1f  %8.1f  %8.1f  %8.1f\n",
               e->apid, e->param_name, e->units,
               e->red_low, e->yellow_low, e->yellow_high, e->red_high);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main(void) {
    demo_static_tables();
    demo_json_tables("mission_config.json");
    return 0;
}
