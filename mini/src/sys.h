#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Minimal MVP snapshot. Future iterations grow this struct (battery, gpu,
 * temps, disks, net, …). */
typedef struct {
    /* CPU — overall percent (0..100) plus per-core, up to 32 cores. */
    double cpu_avg;
    double cpu_per[32];
    int    cpu_n;

    /* Memory in gigabytes (1024^3) and percent of total. */
    double mem_total_gb;
    double mem_used_gb;
    double mem_used_pct;
    double swap_used_gb;
    double swap_used_pct;
} sysinfo_t;

/* Initialize internal state (previous CPU counters, etc.). */
void sys_init(void);

/* Poll once. Safe to call repeatedly. The first call returns 0% CPU because
 * we need a baseline; subsequent calls return the load since the previous
 * poll. */
void sys_poll(sysinfo_t *out);
