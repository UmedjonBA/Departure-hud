#include "sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CORES 32

typedef struct {
    unsigned long long total;
    unsigned long long idle;
} cpu_sample_t;

static cpu_sample_t g_prev[MAX_CORES + 1];   /* index 0 = aggregate */
static int          g_prev_n = 0;

void sys_init(void) {
    memset(g_prev, 0, sizeof(g_prev));
    g_prev_n = 0;
}

/* Read /proc/stat. Lines we care about start with "cpu" followed by either
 * a space (aggregate) or a digit (per-core). The columns are:
 *   user nice system idle iowait irq softirq steal guest guest_nice
 * Idle time = idle + iowait. Total = sum of all listed cols. */
static void poll_cpu(sysinfo_t *out) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) { out->cpu_n = 0; out->cpu_avg = 0; return; }

    char line[512];
    cpu_sample_t cur[MAX_CORES + 1];
    int cur_n = 0;
    double pct[MAX_CORES];
    int pct_n = 0;
    double agg_pct = 0;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "cpu", 3) != 0) break;

        int idx;
        if (line[3] == ' ') idx = 0;            /* aggregate */
        else if (line[3] >= '0' && line[3] <= '9') idx = 1 + atoi(line + 3);
        else continue;
        if (idx > MAX_CORES) continue;

        unsigned long long v[10] = {0};
        const char *p = line + 3;
        while (*p == ' ' || (*p >= '0' && *p <= '9' && idx > 0)) {
            if (*p == ' ') { p++; continue; }
            break;
        }
        /* Skip to first digit. */
        while (*p && (*p < '0' || *p > '9')) p++;
        int got = sscanf(p, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                         &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
                         &v[6], &v[7], &v[8], &v[9]);
        if (got < 4) continue;

        unsigned long long total = 0;
        for (int i = 0; i < got; i++) total += v[i];
        unsigned long long idle  = v[3] + v[4];

        cur[idx].total = total;
        cur[idx].idle  = idle;
        if (idx + 1 > cur_n) cur_n = idx + 1;

        double p_pct = 0;
        if (g_prev_n > idx && cur[idx].total > g_prev[idx].total) {
            unsigned long long dt = total - g_prev[idx].total;
            unsigned long long di = idle  - g_prev[idx].idle;
            p_pct = dt ? (1.0 - (double)di / (double)dt) * 100.0 : 0.0;
            if (p_pct < 0)   p_pct = 0;
            if (p_pct > 100) p_pct = 100;
        }
        if (idx == 0) agg_pct = p_pct;
        else if (pct_n < MAX_CORES) pct[pct_n++] = p_pct;
    }
    fclose(f);

    memcpy(g_prev, cur, sizeof(cur));
    g_prev_n = cur_n;

    out->cpu_n   = pct_n;
    out->cpu_avg = agg_pct;
    for (int i = 0; i < pct_n; i++) out->cpu_per[i] = pct[i];
}

/* Read /proc/meminfo. Values are in kB. */
static unsigned long read_meminfo_kb(FILE *f, const char *key) {
    rewind(f);
    char line[256];
    size_t klen = strlen(key);
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, key, klen) == 0 && line[klen] == ':') {
            return strtoul(line + klen + 1, NULL, 10);
        }
    }
    return 0;
}

static void poll_mem(sysinfo_t *out) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return;

    unsigned long total      = read_meminfo_kb(f, "MemTotal");
    unsigned long avail      = read_meminfo_kb(f, "MemAvailable");
    unsigned long swap_total = read_meminfo_kb(f, "SwapTotal");
    unsigned long swap_free  = read_meminfo_kb(f, "SwapFree");
    fclose(f);

    /* Convert from kB to GB (1024^3 byte basis). */
    out->mem_total_gb = total / 1024.0 / 1024.0;
    out->mem_used_gb  = (total > avail ? (total - avail) : 0) / 1024.0 / 1024.0;
    out->mem_used_pct = total ? (double)(total - avail) / (double)total * 100.0 : 0;

    unsigned long swap_used  = swap_total > swap_free ? swap_total - swap_free : 0;
    out->swap_used_gb  = swap_used / 1024.0 / 1024.0;
    out->swap_used_pct = swap_total ? (double)swap_used / (double)swap_total * 100.0 : 0;
}

void sys_poll(sysinfo_t *out) {
    memset(out, 0, sizeof(*out));
    poll_cpu(out);
    poll_mem(out);
}
