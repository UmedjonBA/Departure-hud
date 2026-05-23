#define _GNU_SOURCE
#include "sys.h"

#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

/* ── Internal state ─────────────────────────────────────────────────── */

typedef struct {
    unsigned long long total, idle;
} cpu_sample_t;

static struct {
    cpu_sample_t  cpu_prev[SYS_MAX_CORES + 1];   /* index 0 = aggregate */
    int           cpu_prev_n;

    unsigned long long net_rx_prev, net_tx_prev;
    struct timespec    net_t_prev;
    char               net_iface_pick[SYS_STR_MAX];

    const char *const *disks_to_show;
    /* The discovered hwmon node paths, cached after first call. */
    char        hwmon_cpu  [128];
    char        hwmon_gpu  [128];
    char        hwmon_ssd  [128];
    char        hwmon_case [128];
    bool        hwmon_scanned;

    char        backlight_dir[128];
    bool        backlight_scanned;
} S;

/* ── Small helpers ──────────────────────────────────────────────────── */

static const char *DEFAULT_DISKS[] = { "/", NULL };

static bool read_file(const char *path, char *buf, size_t cap) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    ssize_t n = read(fd, buf, cap - 1);
    close(fd);
    if (n < 0) return false;
    buf[n] = '\0';
    return true;
}

static long read_long(const char *path) {
    char buf[64];
    if (!read_file(path, buf, sizeof(buf))) return -1;
    return strtol(buf, NULL, 10);
}

static void trim(char *s) {
    char *e = s + strlen(s);
    while (e > s && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
}

static double tsdelta(struct timespec a, struct timespec b) {
    return (a.tv_sec - b.tv_sec) + (a.tv_nsec - b.tv_nsec) / 1e9;
}

/* ── CPU ────────────────────────────────────────────────────────────── */

static void poll_cpu(sysinfo_t *out) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return;
    char line[512];
    cpu_sample_t cur[SYS_MAX_CORES + 1] = {0};
    int cur_n = 0;
    double agg = 0;
    int    pct_n = 0;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "cpu", 3) != 0) break;
        int idx;
        if      (line[3] == ' ')                       idx = 0;
        else if (line[3] >= '0' && line[3] <= '9')     idx = 1 + atoi(line + 3);
        else continue;
        if (idx > SYS_MAX_CORES) continue;

        unsigned long long v[10] = {0};
        const char *p = line + 3;
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
        if (S.cpu_prev_n > idx && cur[idx].total > S.cpu_prev[idx].total) {
            unsigned long long dt = total - S.cpu_prev[idx].total;
            unsigned long long di = idle  - S.cpu_prev[idx].idle;
            p_pct = dt ? (1.0 - (double)di / (double)dt) * 100.0 : 0;
            if (p_pct < 0)   p_pct = 0;
            if (p_pct > 100) p_pct = 100;
        }
        if (idx == 0) agg = p_pct;
        else if (pct_n < SYS_MAX_CORES) out->cpu_per[pct_n++] = p_pct;
    }
    fclose(f);

    memcpy(S.cpu_prev, cur, sizeof(cur));
    S.cpu_prev_n = cur_n;
    out->cpu_n   = pct_n;
    out->cpu_avg = agg;
}

/* Per-core frequencies via the cpufreq sysfs nodes (kHz → GHz). */
static void poll_freq(sysinfo_t *out) {
    char path[128];
    for (int i = 0; i < out->cpu_n && i < SYS_MAX_CORES; i++) {
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);
        long khz = read_long(path);
        if (khz <= 0) continue;
        double ghz = khz / 1e6;
        out->freq_ghz[i] = ghz;
        if (ghz > out->freq_max_ghz) out->freq_max_ghz = ghz;
    }
}

/* ── Memory ─────────────────────────────────────────────────────────── */

static unsigned long meminfo_kb(FILE *f, const char *key) {
    rewind(f);
    char line[256];
    size_t klen = strlen(key);
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, key, klen) == 0 && line[klen] == ':')
            return strtoul(line + klen + 1, NULL, 10);
    }
    return 0;
}

static void poll_mem(sysinfo_t *out) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return;
    unsigned long total = meminfo_kb(f, "MemTotal");
    unsigned long avail = meminfo_kb(f, "MemAvailable");
    unsigned long cache = meminfo_kb(f, "Cached")
                        + meminfo_kb(f, "SReclaimable")
                        + meminfo_kb(f, "Buffers");
    unsigned long s_tot = meminfo_kb(f, "SwapTotal");
    unsigned long s_fre = meminfo_kb(f, "SwapFree");
    fclose(f);

    const double GB = 1024.0 * 1024.0;
    out->mem_total_gb  = total / GB;
    out->mem_used_gb   = (total > avail ? total - avail : 0) / GB;
    out->mem_used_pct  = total ? (double)(total - avail) / total * 100.0 : 0;
    out->mem_cache_gb  = cache / GB;
    out->mem_cache_pct = total ? (double)cache / total * 100.0 : 0;
    unsigned long s_used = s_tot > s_fre ? s_tot - s_fre : 0;
    out->swap_used_gb  = s_used / GB;
    out->swap_used_pct = s_tot ? (double)s_used / s_tot * 100.0 : 0;
}

/* ── Uptime / identity / load ───────────────────────────────────────── */

static void poll_uptime(sysinfo_t *out) {
    char buf[64];
    if (read_file("/proc/uptime", buf, sizeof(buf))) {
        out->uptime_sec = (unsigned long)strtod(buf, NULL);
    }
}

static void poll_loadavg(sysinfo_t *out) {
    char buf[64];
    if (!read_file("/proc/loadavg", buf, sizeof(buf))) return;
    sscanf(buf, "%lf %lf %lf",
           &out->load_avg[0], &out->load_avg[1], &out->load_avg[2]);
}

static void poll_identity(sysinfo_t *out) {
    /* Hostname. */
    if (gethostname(out->host, sizeof(out->host)) != 0)
        snprintf(out->host, sizeof(out->host), "host");
    out->host[sizeof(out->host) - 1] = '\0';

    /* User. */
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_name) {
        snprintf(out->user, sizeof(out->user), "%s", pw->pw_name);
    } else {
        const char *u = getenv("USER");
        snprintf(out->user, sizeof(out->user), "%s", u ? u : "?");
    }

    /* Shell basename. */
    const char *sh = (pw && pw->pw_shell) ? pw->pw_shell : getenv("SHELL");
    if (sh) {
        const char *slash = strrchr(sh, '/');
        snprintf(out->shell, sizeof(out->shell), "%s", slash ? slash + 1 : sh);
    } else {
        snprintf(out->shell, sizeof(out->shell), "sh");
    }

    /* Kernel release. */
    struct utsname un;
    if (uname(&un) == 0) {
        snprintf(out->kernel, sizeof(out->kernel), "%s", un.release);
    }
}

/* ── Net ────────────────────────────────────────────────────────────── */

static bool skip_iface(const char *name) {
    static const char *skip[] = { "lo", "docker", "br-", "veth", "virbr",
                                  "vmnet", "vnet", "tun", "tap", NULL };
    for (int i = 0; skip[i]; i++)
        if (strncmp(name, skip[i], strlen(skip[i])) == 0) return true;
    return false;
}

/* Pick the interface carrying the most bytes (rx+tx). Simple and works on
 * laptops where exactly one of wifi/eth/wwan is active. */
static void poll_net(sysinfo_t *out) {
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) return;

    char   line[512];
    /* Skip two header lines. */
    if (!fgets(line, sizeof(line), f) || !fgets(line, sizeof(line), f)) {
        fclose(f); return;
    }

    char   best_name[SYS_STR_MAX] = "";
    unsigned long long best_rx = 0, best_tx = 0;
    unsigned long long best_total = 0;

    while (fgets(line, sizeof(line), f)) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        char name[SYS_STR_MAX]; const char *src = line;
        while (*src == ' ' || *src == '\t') src++;
        snprintf(name, sizeof(name), "%s", src);
        if (skip_iface(name)) continue;

        unsigned long long rx, packets, errs, drop, fifo, frame, compr, multi;
        unsigned long long tx_bytes, tx_packets;
        int got = sscanf(colon + 1,
            "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
            &rx, &packets, &errs, &drop, &fifo, &frame, &compr, &multi,
            &tx_bytes, &tx_packets);
        if (got < 9) continue;
        (void)packets; (void)errs; (void)drop; (void)fifo;
        (void)frame; (void)compr; (void)multi; (void)tx_packets;

        unsigned long long total = rx + tx_bytes;
        if (total > best_total) {
            best_total = total;
            best_rx = rx;
            best_tx = tx_bytes;
            snprintf(best_name, sizeof(best_name), "%s", name);
        }
    }
    fclose(f);

    if (best_name[0] == '\0') {
        out->net_active = false;
        out->net_iface[0] = '\0';
        out->net_down_bps = 0;
        out->net_up_bps   = 0;
        return;
    }

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    /* Reset baseline if the active interface changed. */
    if (strcmp(best_name, S.net_iface_pick) != 0) {
        snprintf(S.net_iface_pick, sizeof(S.net_iface_pick), "%s", best_name);
        S.net_rx_prev = best_rx;
        S.net_tx_prev = best_tx;
        S.net_t_prev  = now;
        out->net_down_bps = 0;
        out->net_up_bps   = 0;
    } else {
        double dt = tsdelta(now, S.net_t_prev);
        if (dt > 0.05) {
            double drx = (double)(best_rx > S.net_rx_prev ? best_rx - S.net_rx_prev : 0);
            double dtx = (double)(best_tx > S.net_tx_prev ? best_tx - S.net_tx_prev : 0);
            out->net_down_bps = drx / dt;
            out->net_up_bps   = dtx / dt;
            S.net_rx_prev = best_rx;
            S.net_tx_prev = best_tx;
            S.net_t_prev  = now;
        }
    }
    snprintf(out->net_iface, sizeof(out->net_iface), "%s", best_name);
    out->net_active = true;
}

/* ── Disks ──────────────────────────────────────────────────────────── */

static void poll_disks(sysinfo_t *out) {
    const char *const *list = S.disks_to_show ? S.disks_to_show : DEFAULT_DISKS;
    out->disks_n = 0;
    const double GB = 1024.0 * 1024.0 * 1024.0;
    for (int i = 0; list[i] && out->disks_n < SYS_MAX_DISKS; i++) {
        struct statvfs s;
        if (statvfs(list[i], &s) != 0) continue;
        sys_disk_t *d = &out->disks[out->disks_n++];
        snprintf(d->mount, sizeof(d->mount), "%s", list[i]);
        unsigned long long total = (unsigned long long)s.f_blocks * s.f_frsize;
        unsigned long long avail = (unsigned long long)s.f_bavail * s.f_frsize;
        unsigned long long used  = total > avail ? total - avail : 0;
        d->total_gb = total / GB;
        d->used_gb  = used  / GB;
        d->used_pct = total ? (double)used / total * 100.0 : 0;
    }
}

/* ── Temps via /sys/class/hwmon ─────────────────────────────────────── */

/* Read first temp*_input that matches `label_substr` in its label, or just
 * the first temp1_input if label_substr is NULL. Returns °C, or -1. */
static double hwmon_first_temp(const char *hwmon_dir, const char *label_substr) {
    DIR *d = opendir(hwmon_dir);
    if (!d) return -1;
    double pick = -1;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "temp", 4) != 0) continue;
        if (!strstr(e->d_name, "_input")) continue;
        char ipath[256], lpath[256], buf[128];
        snprintf(ipath, sizeof(ipath), "%s/%s", hwmon_dir, e->d_name);
        snprintf(lpath, sizeof(lpath), "%s/%.*s_label", hwmon_dir,
                 (int)(strchr(e->d_name, '_') - e->d_name), e->d_name);
        if (label_substr) {
            if (!read_file(lpath, buf, sizeof(buf))) continue;
            trim(buf);
            if (!strcasestr(buf, label_substr)) continue;
        }
        long mC = read_long(ipath);
        if (mC > 0) { pick = mC / 1000.0; break; }
    }
    closedir(d);
    return pick;
}

static void scan_hwmon(void) {
    if (S.hwmon_scanned) return;
    S.hwmon_scanned = true;
    DIR *d = opendir("/sys/class/hwmon");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char dir[128], name[64];
        snprintf(dir, sizeof(dir), "/sys/class/hwmon/%s", e->d_name);
        char npath[160];
        snprintf(npath, sizeof(npath), "%s/name", dir);
        if (!read_file(npath, name, sizeof(name))) continue;
        trim(name);

        /* Common chip names: coretemp/k10temp/zenpower for CPU,
         * amdgpu/nvidia/i915/nouveau for GPU, nvme/drivetemp for SSD,
         * acpitz/thinkpad/nct* for chassis. */
        if (!*S.hwmon_cpu &&
            (strstr(name, "coretemp") || strstr(name, "k10temp") ||
             strstr(name, "zenpower") || strstr(name, "cpu_thermal"))) {
            snprintf(S.hwmon_cpu, sizeof(S.hwmon_cpu), "%s", dir);
        }
        if (!*S.hwmon_gpu &&
            (strstr(name, "amdgpu") || strstr(name, "nvidia") ||
             strstr(name, "nouveau") || strstr(name, "i915") ||
             strstr(name, "xe"))) {
            snprintf(S.hwmon_gpu, sizeof(S.hwmon_gpu), "%s", dir);
        }
        if (!*S.hwmon_ssd &&
            (strstr(name, "nvme") || strstr(name, "drivetemp"))) {
            snprintf(S.hwmon_ssd, sizeof(S.hwmon_ssd), "%s", dir);
        }
        if (!*S.hwmon_case &&
            (strstr(name, "acpitz") || strstr(name, "thinkpad") ||
             strstr(name, "asus") || strstr(name, "wmi"))) {
            snprintf(S.hwmon_case, sizeof(S.hwmon_case), "%s", dir);
        }
    }
    closedir(d);
}

static void poll_temps(sysinfo_t *out) {
    scan_hwmon();
    if (*S.hwmon_cpu) {
        double t = hwmon_first_temp(S.hwmon_cpu, "Package");
        if (t < 0) t = hwmon_first_temp(S.hwmon_cpu, "Tctl");
        if (t < 0) t = hwmon_first_temp(S.hwmon_cpu, NULL);
        if (t > 0) { out->has_cpu_temp = true; out->cpu_temp_c = t; }
    }
    if (*S.hwmon_gpu) {
        double t = hwmon_first_temp(S.hwmon_gpu, "edge");
        if (t < 0) t = hwmon_first_temp(S.hwmon_gpu, NULL);
        if (t > 0) { out->has_gpu_temp = true; out->gpu_temp_c = t; }
    }
    if (*S.hwmon_ssd) {
        double t = hwmon_first_temp(S.hwmon_ssd, "Composite");
        if (t < 0) t = hwmon_first_temp(S.hwmon_ssd, NULL);
        if (t > 0) { out->has_ssd_temp = true; out->ssd_temp_c = t; }
    }
    if (*S.hwmon_case) {
        double t = hwmon_first_temp(S.hwmon_case, NULL);
        if (t > 0) { out->has_case_temp = true; out->case_temp_c = t; }
    }
}

/* ── Battery ────────────────────────────────────────────────────────── */

static bool get_uevent(const char *dir, const char *key, char *out, size_t cap) {
    char path[256];
    snprintf(path, sizeof(path), "%s/uevent", dir);
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[256];
    size_t klen = strlen(key);
    bool ok = false;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
            snprintf(out, cap, "%s", line + klen + 1);
            trim(out);
            ok = true;
            break;
        }
    }
    fclose(f);
    return ok;
}

static void poll_battery(sysinfo_t *out) {
    DIR *d = opendir("/sys/class/power_supply");
    if (!d) return;
    struct dirent *e;
    char bdir[128] = "";
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "BAT", 3) == 0) {
            snprintf(bdir, sizeof(bdir),
                     "/sys/class/power_supply/%s", e->d_name);
            break;
        }
    }
    closedir(d);
    if (!*bdir) { out->has_battery = false; return; }

    char buf[64];
    if (!get_uevent(bdir, "POWER_SUPPLY_PRESENT", buf, sizeof(buf)) ||
        strcmp(buf, "1") != 0) {
        out->has_battery = false;
        return;
    }
    out->has_battery = true;

    if (get_uevent(bdir, "POWER_SUPPLY_CAPACITY", buf, sizeof(buf)))
        out->bat_pct = strtod(buf, NULL);

    if (get_uevent(bdir, "POWER_SUPPLY_STATUS", buf, sizeof(buf))) {
        for (char *p = buf; *p; p++) *p = (char)toupper((unsigned char)*p);
        snprintf(out->bat_state, sizeof(out->bat_state), "%s", buf);
    } else {
        snprintf(out->bat_state, sizeof(out->bat_state), "UNKNOWN");
    }

    double rate_w = 0;
    if (get_uevent(bdir, "POWER_SUPPLY_POWER_NOW", buf, sizeof(buf))) {
        rate_w = strtod(buf, NULL) / 1e6;
    } else {
        double I = 0, V = 0;
        if (get_uevent(bdir, "POWER_SUPPLY_CURRENT_NOW", buf, sizeof(buf)))
            I = strtod(buf, NULL) / 1e6;
        if (get_uevent(bdir, "POWER_SUPPLY_VOLTAGE_NOW", buf, sizeof(buf)))
            V = strtod(buf, NULL) / 1e6;
        rate_w = I * V;
    }
    if      (strcmp(out->bat_state, "DISCHARGING") == 0) rate_w = -fabs(rate_w);
    else if (strcmp(out->bat_state, "CHARGING")    == 0) rate_w =  fabs(rate_w);
    out->bat_rate_w = rate_w;

    /* Time-left: pick whichever pair of energy/current the kernel exposes. */
    double full = 0, now = 0, rate = 0;
    if (get_uevent(bdir, "POWER_SUPPLY_CHARGE_FULL", buf, sizeof(buf)))
        full = strtod(buf, NULL);
    else if (get_uevent(bdir, "POWER_SUPPLY_ENERGY_FULL", buf, sizeof(buf)))
        full = strtod(buf, NULL);
    if (get_uevent(bdir, "POWER_SUPPLY_CHARGE_NOW", buf, sizeof(buf)))
        now = strtod(buf, NULL);
    else if (get_uevent(bdir, "POWER_SUPPLY_ENERGY_NOW", buf, sizeof(buf)))
        now = strtod(buf, NULL);
    if (get_uevent(bdir, "POWER_SUPPLY_CURRENT_NOW", buf, sizeof(buf)))
        rate = strtod(buf, NULL);
    else if (get_uevent(bdir, "POWER_SUPPLY_POWER_NOW", buf, sizeof(buf)))
        rate = strtod(buf, NULL);

    int minutes = 0;
    if (rate > 0 && now > 0) {
        double hours = 0;
        if      (strcmp(out->bat_state, "DISCHARGING") == 0) hours = now / rate;
        else if (strcmp(out->bat_state, "CHARGING")    == 0 && full > now) hours = (full - now) / rate;
        minutes = (int)(hours * 60);
        if (minutes < 0) minutes = 0;
    }
    out->bat_minutes_left = minutes;

    char cyc[64];
    char cycp[256];
    snprintf(cycp, sizeof(cycp), "%s/cycle_count", bdir);
    if (read_file(cycp, cyc, sizeof(cyc))) out->bat_cycles = atoi(cyc);

    if (strcmp(out->bat_state, "DISCHARGING") == 0 && minutes > 0) {
        out->bat_drain_pct_h = out->bat_pct * 60.0 / minutes;
    } else {
        out->bat_drain_pct_h = 0;
    }
}

/* ── Brightness ─────────────────────────────────────────────────────── */

static void scan_backlight(void) {
    if (S.backlight_scanned) return;
    S.backlight_scanned = true;
    DIR *d = opendir("/sys/class/backlight");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        snprintf(S.backlight_dir, sizeof(S.backlight_dir),
                 "/sys/class/backlight/%s", e->d_name);
        break;
    }
    closedir(d);
}

static void poll_brightness(sysinfo_t *out) {
    scan_backlight();
    if (!*S.backlight_dir) return;
    char p1[160], p2[160];
    snprintf(p1, sizeof(p1), "%s/brightness",     S.backlight_dir);
    snprintf(p2, sizeof(p2), "%s/max_brightness", S.backlight_dir);
    long b = read_long(p1), m = read_long(p2);
    if (b >= 0 && m > 0) {
        out->has_brightness = true;
        out->brightness_pct = (int)((double)b / m * 100.0 + 0.5);
    }
}

/* ── GPU (sysfs amdgpu/i915 + dlopen'd NVML) ────────────────────────── */

static bool gpu_from_sysfs(sysinfo_t *out);
static bool gpu_from_nvml (sysinfo_t *out);

/* AMD/Intel via /sys/class/drm/card<N>/device — works without nvidia driver. */
static bool gpu_from_sysfs(sysinfo_t *out) {
    DIR *d = opendir("/sys/class/drm");
    if (!d) return false;
    struct dirent *e;
    bool got = false;
    while ((e = readdir(d))) {
        /* Pick "cardN" (no '-' to skip connectors like card0-DP-1). */
        if (strncmp(e->d_name, "card", 4) != 0) continue;
        if (strchr(e->d_name, '-')) continue;
        char base[160];
        snprintf(base, sizeof(base), "/sys/class/drm/%s/device", e->d_name);

        /* Vendor ID — 0x1002 AMD, 0x10de NVIDIA, 0x8086 Intel. */
        char vendor[16] = "";
        char vpath[200];
        snprintf(vpath, sizeof(vpath), "%s/vendor", base);
        if (read_file(vpath, vendor, sizeof(vendor))) trim(vendor);

        /* gpu_busy_percent is amdgpu/i915. */
        char busyp[200], buf[64];
        snprintf(busyp, sizeof(busyp), "%s/gpu_busy_percent", base);
        if (read_file(busyp, buf, sizeof(buf))) {
            out->gpu_load_pct = strtod(buf, NULL);
            got = true;
        }

        /* Current sclk: "0: 200Mhz\n1: 1500Mhz *\n…" — star marks active. */
        char sclkp[200];
        snprintf(sclkp, sizeof(sclkp), "%s/pp_dpm_sclk", base);
        FILE *f = fopen(sclkp, "r");
        if (f) {
            char ln[128];
            int  max_mhz = 0, cur_mhz = 0;
            while (fgets(ln, sizeof(ln), f)) {
                int mhz = 0;
                if (sscanf(ln, "%*d: %dMhz", &mhz) == 1 ||
                    sscanf(ln, "%*d: %dMHz", &mhz) == 1) {
                    if (mhz > max_mhz) max_mhz = mhz;
                    if (strchr(ln, '*')) cur_mhz = mhz;
                }
            }
            fclose(f);
            if (max_mhz) out->gpu_clock_max_mhz = max_mhz;
            if (cur_mhz) out->gpu_clock_mhz     = cur_mhz;
            got = true;
        }

        if (got) {
            if      (strcmp(vendor, "0x1002") == 0) snprintf(out->gpu_name, sizeof(out->gpu_name), "AMD GPU");
            else if (strcmp(vendor, "0x8086") == 0) snprintf(out->gpu_name, sizeof(out->gpu_name), "Intel GPU");
            else if (strcmp(vendor, "0x10de") == 0) snprintf(out->gpu_name, sizeof(out->gpu_name), "NVIDIA GPU");
            else                                    snprintf(out->gpu_name, sizeof(out->gpu_name), "GPU");
            out->has_gpu = true;
            break;
        }
    }
    closedir(d);
    return got;
}

/* NVIDIA via dlopen'd libnvidia-ml. We never link against it directly so the
 * binary runs fine on AMD-only laptops. */
typedef struct {
    void *handle;
    /* nvmlInit_v2, nvmlShutdown, nvmlDeviceGetHandleByIndex_v2,
     * nvmlDeviceGetUtilizationRates, nvmlDeviceGetTemperature,
     * nvmlDeviceGetClockInfo, nvmlDeviceGetName */
    int (*init_v2)(void);
    int (*shutdown)(void);
    int (*get_handle)(unsigned int, void**);
    int (*get_util)(void*, void*);
    int (*get_temp)(void*, int, unsigned int*);
    int (*get_clock)(void*, int, unsigned int*);
    int (*get_name)(void*, char*, unsigned int);
    void *dev;
    bool  inited;
} nvml_t;
static nvml_t g_nvml;

static bool gpu_nvml_init(void) {
    if (g_nvml.inited)  return true;
    if (g_nvml.handle == (void*)-1) return false;   /* prior failure */
    g_nvml.handle = dlopen("libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!g_nvml.handle) {
        g_nvml.handle = (void*)-1;
        return false;
    }
    #define SYM(d, n) g_nvml.d = dlsym(g_nvml.handle, n)
    SYM(init_v2,    "nvmlInit_v2");
    SYM(shutdown,   "nvmlShutdown");
    SYM(get_handle, "nvmlDeviceGetHandleByIndex_v2");
    SYM(get_util,   "nvmlDeviceGetUtilizationRates");
    SYM(get_temp,   "nvmlDeviceGetTemperature");
    SYM(get_clock,  "nvmlDeviceGetClockInfo");
    SYM(get_name,   "nvmlDeviceGetName");
    #undef SYM
    if (!g_nvml.init_v2 || !g_nvml.get_handle || !g_nvml.get_util ||
        !g_nvml.get_temp || !g_nvml.get_clock) {
        dlclose(g_nvml.handle); g_nvml.handle = (void*)-1; return false;
    }
    if (g_nvml.init_v2() != 0) { g_nvml.handle = (void*)-1; return false; }
    if (g_nvml.get_handle(0, &g_nvml.dev) != 0) {
        if (g_nvml.shutdown) g_nvml.shutdown();
        g_nvml.handle = (void*)-1; return false;
    }
    g_nvml.inited = true;
    return true;
}

static bool gpu_from_nvml(sysinfo_t *out) {
    if (!gpu_nvml_init()) return false;

    /* nvmlUtilization_t = { unsigned int gpu, memory }. */
    struct { unsigned int gpu, memory; } util;
    if (g_nvml.get_util(g_nvml.dev, &util) == 0) {
        out->gpu_load_pct = util.gpu;
    }
    /* NVML_TEMPERATURE_GPU = 0. */
    unsigned int t = 0;
    if (g_nvml.get_temp(g_nvml.dev, 0, &t) == 0 && t > 0) {
        out->has_gpu_temp = true;
        out->gpu_temp_c   = t;
    }
    /* NVML_CLOCK_GRAPHICS = 0, NVML_CLOCK_MAX_GRAPHICS would need separate
     * call; we approximate max as the highest observed clock. */
    unsigned int mhz = 0;
    if (g_nvml.get_clock(g_nvml.dev, 0, &mhz) == 0) {
        out->gpu_clock_mhz = (int)mhz;
        if ((int)mhz > out->gpu_clock_max_mhz) out->gpu_clock_max_mhz = mhz;
    }
    if (g_nvml.get_name) {
        char name[SYS_STR_MAX] = "";
        if (g_nvml.get_name(g_nvml.dev, name, sizeof(name)) == 0 && *name) {
            snprintf(out->gpu_name, sizeof(out->gpu_name), "%s", name);
        } else {
            snprintf(out->gpu_name, sizeof(out->gpu_name), "NVIDIA");
        }
    }
    out->has_gpu = true;
    return true;
}

void sys_poll_gpu(sysinfo_t *out) {
    /* Try sysfs first — amdgpu/nouveau expose gpu_busy_percent and clock
     * tables, so we get full stats without loading anything. NVML is opt-in
     * via DEPARTURE_HUD_NVML=1 because libnvidia-ml drags in libcuda
     * (~6 MB resident, ~90 MB virtual) which is a steep price to pay just
     * to read a utilization percentage on hybrid laptops where the iGPU
     * does the actual rendering. */
    if (gpu_from_sysfs(out)) return;
    const char *nv = getenv("DEPARTURE_HUD_NVML");
    if (nv && nv[0] && nv[0] != '0') gpu_from_nvml(out);
}

/* ── Public API ─────────────────────────────────────────────────────── */

void sys_init(const sys_opts_t *opts) {
    memset(&S, 0, sizeof(S));
    if (opts && opts->disks_to_show) S.disks_to_show = opts->disks_to_show;
}

void sys_poll_fast(sysinfo_t *out) {
    /* Caller is responsible for memset; we preserve previously-populated
     * slow-poll fields between fast ticks. */
    poll_cpu(out);
    poll_freq(out);
    poll_mem(out);
    poll_net(out);
    poll_uptime(out);
    poll_loadavg(out);
}

void sys_poll_slow(sysinfo_t *out) {
    poll_identity(out);
    poll_disks(out);
    poll_temps(out);
    poll_battery(out);
    poll_brightness(out);
}
