#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SYS_MAX_CORES  32
#define SYS_MAX_DISKS  8
#define SYS_STR_MAX    64

typedef struct {
    char    mount[SYS_STR_MAX];
    double  used_pct;
    double  used_gb;
    double  total_gb;
} sys_disk_t;

typedef struct {
    /* ── CPU ──────────────────────────────────────────────── */
    double cpu_avg;                       /* aggregate % */
    double cpu_per[SYS_MAX_CORES];        /* per-core %  */
    int    cpu_n;                         /* core count  */
    double freq_ghz[SYS_MAX_CORES];       /* per-core GHz */
    double freq_max_ghz;                  /* observed max */

    /* ── Memory ───────────────────────────────────────────── */
    double mem_total_gb;
    double mem_used_gb;
    double mem_used_pct;
    double mem_cache_gb;
    double mem_cache_pct;
    double swap_used_gb;
    double swap_used_pct;

    /* ── Uptime / identity ────────────────────────────────── */
    unsigned long uptime_sec;
    double  load_avg[3];                  /* 1, 5, 15 min */
    char    host[SYS_STR_MAX];
    char    user[SYS_STR_MAX];
    char    shell[SYS_STR_MAX];           /* basename only */
    char    kernel[SYS_STR_MAX];

    /* ── Net (single primary interface) ───────────────────── */
    bool    net_active;
    char    net_iface[SYS_STR_MAX];
    double  net_down_bps;
    double  net_up_bps;

    /* ── Disks ────────────────────────────────────────────── */
    sys_disk_t disks[SYS_MAX_DISKS];
    int        disks_n;

    /* ── Temps (°C) ───────────────────────────────────────── */
    bool    has_cpu_temp;
    double  cpu_temp_c;
    bool    has_gpu_temp;
    double  gpu_temp_c;
    bool    has_ssd_temp;
    double  ssd_temp_c;
    bool    has_case_temp;
    double  case_temp_c;

    /* ── Battery ──────────────────────────────────────────── */
    bool    has_battery;
    double  bat_pct;
    char    bat_state[SYS_STR_MAX];       /* CHARGING / DISCHARGING / FULL */
    double  bat_rate_w;                   /* signed: −draw / +charge */
    int     bat_minutes_left;
    int     bat_cycles;
    double  bat_drain_pct_h;              /* derived: %/h while discharging */

    /* ── Backlight ────────────────────────────────────────── */
    bool    has_brightness;
    int     brightness_pct;

    /* ── GPU (NVML or sysfs; populated by sys_poll_gpu) ───── */
    bool    has_gpu;
    char    gpu_name[SYS_STR_MAX];
    double  gpu_load_pct;
    int     gpu_clock_mhz;
    int     gpu_clock_max_mhz;
} sysinfo_t;

/* Configuration knobs. Pass NULL for defaults. */
typedef struct {
    /* List of mount points to show, NULL-terminated. Default: {"/", NULL}. */
    const char *const *disks_to_show;
    /* Comma-separated regex-ish substrings for the network interface filter.
     * Default: skip "lo", "docker", "br-", "veth", "virbr". */
    const char        *net_skip_prefixes;
} sys_opts_t;

void sys_init(const sys_opts_t *opts);

/* Cheap polls — call every tick (~100 ms). */
void sys_poll_fast(sysinfo_t *out);
/* Heavy polls — temps, disks, battery; call every ~1 s. */
void sys_poll_slow(sysinfo_t *out);

/* Optional: NVIDIA via dlopen'd NVML + AMD/Intel via sysfs. Safe no-op when
 * neither is available. Call from the slow tick. */
void sys_poll_gpu(sysinfo_t *out);
