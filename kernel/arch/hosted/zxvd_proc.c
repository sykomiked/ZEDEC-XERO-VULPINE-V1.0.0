/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_proc.c — the machine's processes (see zxvd_proc.h P1-P5). */
#if !defined(_WIN32)
#    define _DEFAULT_SOURCE
#    define _DARWIN_C_SOURCE
#    include <sys/types.h>
#    include <sys/stat.h>
#    include <sys/wait.h>
#    include <dirent.h>
#    include <errno.h>
#    include <fcntl.h>
#    include <pwd.h>
#    include <signal.h>
#    include <unistd.h>
#    if defined(__APPLE__)
#        include <libproc.h>
#        include <mach/mach_time.h>
#        include <sys/proc.h>
#        include <sys/proc_info.h>
#        include <sys/sysctl.h>
#    endif
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxvd_proc.h"
#include "zxvd_shell.h" /* zxvd_shell_job_of */
#include "zxvd_log.h"   /* zxvd_mono_us */

#define E_PARAMS (-32602)

#if defined(_WIN32) || !(defined(__linux__) || defined(__APPLE__))

void zxvd_proc_enable(bool on)
{
    (void) on;
}
void zxvd_proc_service(void) {}
void zxvd_proc_close(void) {}

static void not_here(zxvd_call_t *c)
{
    zxvd_fail(c, ZXVD_E_PROC, "proc.* is not on this system yet");
}
void zxvd_proc_list(zxvd_call_t *c)
{
    not_here(c);
}
void zxvd_proc_signal(zxvd_call_t *c)
{
    not_here(c);
}
void zxvd_proc_restart(zxvd_call_t *c)
{
    not_here(c);
}

#else

#    define SCAN_MAX 8192u /* processes looked at in one call */

typedef struct {
    long pid, ppid, pgid, sid;
    long uid;
    char name[64];
    char state; /* r s t z i ? */
    uint64_t cpu_ms, rss;
    int64_t started_ms;
} pinfo_t;

static bool ON;

void zxvd_proc_enable(bool on)
{
    ON = on;
}

static uint64_t mono_ms(void)
{
    return zxvd_mono_us() / 1000u;
}

/* ===================== reading a process ===================== */

#    if defined(__linux__)

static long CLK, PAGE;
static int64_t BOOT_MS = -1;

static void linux_init(void)
{
    if (BOOT_MS >= 0) return;
    CLK = sysconf(_SC_CLK_TCK);
    PAGE = sysconf(_SC_PAGESIZE);
    if (CLK <= 0) CLK = 100;
    if (PAGE <= 0) PAGE = 4096;
    BOOT_MS = 0;
    FILE *f = fopen("/proc/stat", "r");
    char line[256];
    while (f && fgets(line, sizeof line, f))
        if (!strncmp(line, "btime ", 6)) {
            BOOT_MS = (int64_t) strtoll(line + 6, NULL, 10) * 1000;
            break;
        }
    if (f) fclose(f);
}

static bool read_proc(long pid, pinfo_t *p)
{
    char path[64], buf[1024];
    struct stat st;
    snprintf(path, sizeof path, "/proc/%ld", pid);
    if (stat(path, &st) != 0) return false;
    snprintf(path, sizeof path, "/proc/%ld/stat", pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) return false;
    buf[n] = 0;
    char *open_ = strchr(buf, '('), *close_ = strrchr(buf, ')');
    if (!open_ || !close_ || close_ < open_ || close_[1] != ' ') return false;
    memset(p, 0, sizeof *p);
    size_t nn = (size_t) (close_ - open_ - 1);
    if (nn >= sizeof p->name) nn = sizeof p->name - 1;
    memcpy(p->name, open_ + 1, nn);
    /* the fields after the name, numbered as proc(5) does from 3 */
    unsigned long long f[25] = {0};
    char st_ch = 0;
    char *s = close_ + 2;
    st_ch = *s;
    s++;
    for (int i = 4; i <= 24 && *s; i++) {
        while (*s == ' ') s++;
        char *end;
        f[i] = strtoull(s, &end, 10);
        if (end == s && *s != '-') return false;
        if (*s == '-') { /* a negative field (priority, nice): not used */
            f[i] = 0;
            end = s + 1;
            while (*end >= '0' && *end <= '9') end++;
        }
        s = end;
    }
    p->pid = pid;
    p->ppid = (long) f[4];
    p->pgid = (long) f[5];
    p->sid = (long) f[6];
    p->uid = (long) st.st_uid;
    p->state = st_ch == 'R'                   ? 'r'
               : st_ch == 'S' || st_ch == 'D' ? 's'
               : st_ch == 'T' || st_ch == 't' ? 't'
               : st_ch == 'Z'                 ? 'z'
               : st_ch == 'I'                 ? 'i'
                                              : '?';
    p->cpu_ms = (uint64_t) ((f[14] + f[15]) * 1000ull / (unsigned long long) CLK);
    p->started_ms = BOOT_MS + (int64_t) (f[22] * 1000ull / (unsigned long long) CLK);
    p->rss = (uint64_t) f[24] * (uint64_t) PAGE;
    return true;
}

static uint32_t list_pids(long *out, uint32_t cap)
{
    linux_init();
    DIR *d = opendir("/proc");
    uint32_t n = 0;
    struct dirent *e;
    while (d && (e = readdir(d)) && n < cap) {
        char *end;
        long pid = strtol(e->d_name, &end, 10);
        if (*end || pid <= 0) continue;
        out[n++] = pid;
    }
    if (d) closedir(d);
    return n;
}

/* the program, its arguments (NUL-separated, *len bytes, *argc of them) and
 * its folder */
static bool read_cmd(long pid, char *exe, size_t exe_cap, char *args, size_t *len, int *argc,
                     char *cwd, size_t cwd_cap)
{
    char path[64];
    snprintf(path, sizeof path, "/proc/%ld/exe", pid);
    ssize_t k = readlink(path, exe, exe_cap - 1);
    if (k <= 0 || (size_t) k >= exe_cap - 1) return false;
    exe[k] = 0;
    if (strstr(exe, " (deleted)")) return false; /* the program file is gone */
    snprintf(path, sizeof path, "/proc/%ld/cwd", pid);
    k = readlink(path, cwd, cwd_cap - 1);
    if (k <= 0 || (size_t) k >= cwd_cap - 1) return false;
    cwd[k] = 0;
    snprintf(path, sizeof path, "/proc/%ld/cmdline", pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    size_t n = 0;
    for (ssize_t r; n < ZXVD_PROC_ARGV_MAX && (r = read(fd, args + n, ZXVD_PROC_ARGV_MAX - n)) > 0;)
        n += (size_t) r;
    char more;
    bool too_long = n == ZXVD_PROC_ARGV_MAX && read(fd, &more, 1) > 0;
    close(fd);
    if (!n || too_long || args[n - 1] != 0) return false;
    *argc = 0;
    for (size_t i = 0; i < n; i++) *argc += args[i] == 0;
    *len = n;
    return true;
}

#    else /* __APPLE__ */

static mach_timebase_info_data_t TB;

/* macOS reports SRUN, SSLEEP, SSTOP, SZOMB and SIDL as the BSD status */
static char state_of(uint32_t st)
{
    return st == SRUN     ? 'r'
           : st == SSLEEP ? 's'
           : st == SSTOP  ? 't'
           : st == SZOMB  ? 'z'
           : st == SIDL   ? 'i'
                          : '?';
}

/* Another user's process (launchd, pid 1, among them): the full BSD info is
 * refused, the short one is not; it has no start time or CPU figures. */
static bool read_short(long pid, pinfo_t *p)
{
    struct proc_bsdshortinfo si;
    if (proc_pidinfo((int) pid, PROC_PIDT_SHORTBSDINFO, 0, &si, PROC_PIDT_SHORTBSDINFO_SIZE) !=
        PROC_PIDT_SHORTBSDINFO_SIZE)
        return false;
    memset(p, 0, sizeof *p);
    p->pid = pid;
    p->ppid = (long) si.pbsi_ppid;
    p->pgid = (long) si.pbsi_pgid;
    p->uid = (long) si.pbsi_uid;
    pid_t sid = getsid((pid_t) pid);
    p->sid = sid < 0 ? -1 : (long) sid;
    size_t nn = strnlen(si.pbsi_comm, sizeof si.pbsi_comm);
    if (nn >= sizeof p->name) nn = sizeof p->name - 1;
    memcpy(p->name, si.pbsi_comm, nn);
    p->state = state_of(si.pbsi_status);
    return true;
}

static bool read_proc(long pid, pinfo_t *p)
{
    struct proc_bsdinfo bi;
    if (proc_pidinfo((int) pid, PROC_PIDTBSDINFO, 0, &bi, PROC_PIDTBSDINFO_SIZE) !=
        PROC_PIDTBSDINFO_SIZE)
        return read_short(pid, p);
    memset(p, 0, sizeof *p);
    p->pid = pid;
    p->ppid = (long) bi.pbi_ppid;
    p->pgid = (long) bi.pbi_pgid;
    p->uid = (long) bi.pbi_uid;
    pid_t sid = getsid((pid_t) pid);
    p->sid = sid < 0 ? -1 : (long) sid;
    const char *nm = bi.pbi_name[0] ? bi.pbi_name : bi.pbi_comm;
    size_t nn = strnlen(nm, bi.pbi_name[0] ? sizeof bi.pbi_name : sizeof bi.pbi_comm);
    if (nn >= sizeof p->name) nn = sizeof p->name - 1;
    memcpy(p->name, nm, nn);
    p->state = state_of(bi.pbi_status);
    p->started_ms = (int64_t) bi.pbi_start_tvsec * 1000 + (int64_t) bi.pbi_start_tvusec / 1000;
    struct proc_taskinfo ti;
    if (proc_pidinfo((int) pid, PROC_PIDTASKINFO, 0, &ti, PROC_PIDTASKINFO_SIZE) ==
        PROC_PIDTASKINFO_SIZE) {
        if (!TB.denom) mach_timebase_info(&TB);
        uint64_t t = ti.pti_total_user + ti.pti_total_system; /* mach time units */
        uint64_t ns = TB.denom ? t / TB.denom * TB.numer + t % TB.denom * TB.numer / TB.denom : 0;
        p->cpu_ms = ns / 1000000u;
        p->rss = ti.pti_resident_size;
    }
    return true;
}

static uint32_t list_pids(long *out, uint32_t cap)
{
    static pid_t pids[SCAN_MAX];
    int n = proc_listallpids(pids, (int) sizeof pids);
    uint32_t k = 0;
    for (int i = 0; i < n && k < cap; i++)
        if (pids[i] > 0) out[k++] = (long) pids[i];
    return k;
}

static bool read_cmd(long pid, char *exe, size_t exe_cap, char *args, size_t *len, int *argc,
                     char *cwd, size_t cwd_cap)
{
    if (exe_cap < PROC_PIDPATHINFO_MAXSIZE || proc_pidpath((int) pid, exe, (uint32_t) exe_cap) <= 0)
        return false;
    struct proc_vnodepathinfo vp;
    if (proc_pidinfo((int) pid, PROC_PIDVNODEPATHINFO, 0, &vp, sizeof vp) != sizeof vp)
        return false;
    size_t cn = strnlen(vp.pvi_cdir.vip_path, sizeof vp.pvi_cdir.vip_path);
    if (!cn || cn >= cwd_cap) return false;
    memcpy(cwd, vp.pvi_cdir.vip_path, cn);
    cwd[cn] = 0;
    /* KERN_PROCARGS2: argc, the exec path, NULs, then argc arguments */
    static char raw[ZXVD_PROC_ARGV_MAX + 4096];
    int mib[3] = {CTL_KERN, KERN_PROCARGS2, (int) pid};
    size_t rn = sizeof raw;
    if (sysctl(mib, 3, raw, &rn, NULL, 0) != 0 || rn < sizeof(int)) return false;
    int ac;
    memcpy(&ac, raw, sizeof ac);
    size_t at = sizeof ac;
    while (at < rn && raw[at]) at++; /* the exec path */
    while (at < rn && !raw[at]) at++;
    size_t start = at;
    for (int i = 0; i < ac; i++) {
        while (at < rn && raw[at]) at++;
        if (at >= rn) return false;
        at++;
    }
    if (ac < 1 || at - start > ZXVD_PROC_ARGV_MAX) return false;
    memcpy(args, raw + start, at - start);
    *len = at - start;
    *argc = ac;
    return true;
}

#    endif

/* ===================== account names ===================== */

static const char *user_name(long uid)
{
    static struct {
        long uid;
        bool ok;
        char name[64];
    } C[16];
    static uint32_t next;
    for (uint32_t i = 0; i < 16; i++)
        if (C[i].name[0] && C[i].uid == uid) return C[i].ok ? C[i].name : NULL;
    struct passwd pw, *res = NULL;
    char buf[2048];
    uint32_t i = next++ % 16;
    C[i].uid = uid;
    C[i].ok = getpwuid_r((uid_t) uid, &pw, buf, sizeof buf, &res) == 0 && res &&
              strlen(pw.pw_name) < sizeof C[i].name;
    snprintf(C[i].name, sizeof C[i].name, "%s", C[i].ok ? pw.pw_name : "?");
    return C[i].ok ? C[i].name : NULL;
}

/* ===================== restarts (P3) ===================== */

typedef struct {
    uint32_t id; /* 0: a free place */
    long old_pid, new_pid;
    int64_t old_started_ms;
    int state; /* R_STOPPING, R_STARTED, R_FAILED */
    uint64_t kill_ms, give_up_ms;
    char error[96];
    char exe[4096], cwd[4096]; /* macOS's PROC_PIDPATHINFO_MAXSIZE */
    char *args;                /* malloc'd, NUL-separated */
    int argc;
} restart_t;

enum { R_STOPPING, R_STARTED, R_FAILED };

static restart_t R[ZXVD_PROC_RESTARTS];
static uint32_t NEXT_R = 1;

static void r_free(restart_t *r)
{
    free(r->args);
    r->args = NULL;
}

static void r_fail(restart_t *r, const char *why)
{
    r->state = R_FAILED;
    snprintf(r->error, sizeof r->error, "%s", why);
    r_free(r);
    zxvd_log(ZXVD_LOG_WARN, "proc", "restart %u of %ld failed: %s", r->id, r->old_pid, why);
}

/* the process is gone: no such pid, a zombie, or the pid now another
 * process (another start time) */
static bool gone(long pid, int64_t started_ms)
{
    if (kill((pid_t) pid, 0) != 0 && errno == ESRCH) return true;
    pinfo_t p;
    /* a pid this zxvd may signal but cannot read has exited: macOS keeps no
     * task, so no BSD info, for a zombie */
    if (!read_proc(pid, &p)) return kill((pid_t) pid, 0) != 0 ? errno == ESRCH : true;
    return p.state == 'z' || p.started_ms != started_ms;
}

/* Start r's program on its own: a new session, /dev/null for 0-2. The
 * middle process reports the program's pid and the exec's outcome. */
static void r_start(restart_t *r)
{
    static char *argv[ZXVD_PROC_ARGV_MAX / 2 + 1];
    if (r->argc < 1 || (size_t) r->argc > sizeof argv / sizeof argv[0] - 1)
        return r_fail(r, "too many arguments");
    char *a = r->args;
    for (int i = 0; i < r->argc; i++) {
        argv[i] = a;
        a += strlen(a) + 1;
    }
    argv[r->argc] = NULL;
    int p[2];
    if (pipe(p) != 0) return r_fail(r, "no pipe");
    fcntl(p[0], F_SETFD, FD_CLOEXEC);
    fcntl(p[1], F_SETFD, FD_CLOEXEC);
    pid_t mid = fork();
    if (mid == 0) {
        setsid();
        pid_t g = fork();
        if (g != 0) {
            long gl = (long) g;
            ssize_t w = write(p[1], &gl, sizeof gl);
            (void) w;
            _exit(g > 0 ? 0 : 1);
        }
        close(p[0]);
        int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) {
            dup2(nul, 0);
            dup2(nul, 1);
            dup2(nul, 2);
            if (nul > 2) close(nul);
        }
        signal(SIGPIPE, SIG_DFL);
        if (chdir(r->cwd) == 0) execv(r->exe, argv);
        int e = errno;
        ssize_t w = write(p[1], &e, sizeof e);
        (void) w;
        _exit(127);
    }
    close(p[1]);
    if (mid < 0) {
        close(p[0]);
        return r_fail(r, "could not fork");
    }
    int st;
    while (waitpid(mid, &st, 0) < 0 && errno == EINTR) {
    }
    long g = -1;
    int e = 0;
    ssize_t k = read(p[0], &g, sizeof g);
    ssize_t k2 = k == (ssize_t) sizeof g && g > 0 ? read(p[0], &e, sizeof e) : 0;
    close(p[0]);
    if (k != (ssize_t) sizeof g || g <= 0) return r_fail(r, "could not fork");
    if (k2 == (ssize_t) sizeof e) {
        char why[96];
        snprintf(why, sizeof why, "could not start it: %s", strerror(e));
        return r_fail(r, why);
    }
    r->new_pid = g;
    r->state = R_STARTED;
    r_free(r);
    zxvd_log(ZXVD_LOG_INFO, "proc", "restart %u: %ld is now %ld", r->id, r->old_pid, g);
}

void zxvd_proc_service(void)
{
    uint64_t now = mono_ms();
    for (uint32_t i = 0; i < ZXVD_PROC_RESTARTS; i++) {
        restart_t *r = &R[i];
        if (!r->id || r->state != R_STOPPING) continue;
        if (gone(r->old_pid, r->old_started_ms))
            r_start(r);
        else if (now >= r->give_up_ms)
            r_fail(r, "it did not stop");
        else if (r->kill_ms && now >= r->kill_ms) {
            kill((pid_t) r->old_pid, SIGKILL);
            r->kill_ms = 0;
        }
    }
}

void zxvd_proc_close(void)
{
    for (uint32_t i = 0; i < ZXVD_PROC_RESTARTS; i++) r_free(&R[i]);
    memset(R, 0, sizeof R);
}

/* ===================== the methods ===================== */

static pinfo_t P[SCAN_MAX];

static int by_pid(const void *a, const void *b)
{
    long x = ((const pinfo_t *) a)->pid, y = ((const pinfo_t *) b)->pid;
    return x < y ? -1 : x > y;
}

static const pinfo_t *find_p(long pid, uint32_t n)
{
    pinfo_t key;
    key.pid = pid;
    return bsearch(&key, P, n, sizeof P[0], by_pid);
}

static const char *state_name(char s)
{
    switch (s) {
    case 'r':
        return "running";
    case 's':
        return "sleeping";
    case 't':
        return "stopped";
    case 'z':
        return "zombie";
    case 'i':
        return "idle";
    default:
        return "other";
    }
}

static void put_user(zxvd_buf_t *b, long uid)
{
    const char *u = uid >= 0 ? user_name(uid) : NULL;
    if (u)
        zxvd_buf_json_str(b, u);
    else
        zxvd_buf_raw(b, "null", 4);
}

void zxvd_proc_list(zxvd_call_t *c)
{
    static long pids[SCAN_MAX];
    bool all = false;
    if (!zxvd_param_bool(c, "all", &all, false)) return;
    zxvd_proc_service();
    uint32_t np = list_pids(pids, SCAN_MAX), n = 0;
    for (uint32_t i = 0; i < np; i++)
        if (read_proc(pids[i], &P[n])) n++;
    qsort(P, n, sizeof P[0], by_pid);
    long me = (long) geteuid();
    zxvd_buf_t *r = zxvd_result(c);
    zxvd_buf_raw(r, "{\"procs\":[", 10);
    uint32_t shown = 0;
    bool truncated = np == SCAN_MAX;
    for (uint32_t i = 0; i < n; i++) {
        const pinfo_t *p = &P[i];
        if (!all && p->uid != me) continue;
        if (shown == ZXVD_PROC_MAX) {
            truncated = true;
            break;
        }
        zxvd_buf_printf(r, "%s{\"pid\":%ld,\"ppid\":%ld,\"name\":", shown ? "," : "", p->pid,
                        p->ppid);
        zxvd_buf_json_str(r, p->name);
        zxvd_buf_raw(r, ",\"user\":", 8);
        put_user(r, p->uid);
        zxvd_buf_printf(r,
                        ",\"uid\":%ld,\"state\":\"%s\",\"cpu_ms\":%llu,\"rss\":%llu,"
                        "\"started_ms\":%lld,\"sid\":",
                        p->uid, state_name(p->state), (unsigned long long) p->cpu_ms,
                        (unsigned long long) p->rss, (long long) p->started_ms);
        if (p->sid > 0)
            zxvd_buf_printf(r, "%ld", p->sid);
        else
            zxvd_buf_raw(r, "null", 4);
        const pinfo_t *lead = p->sid > 0 ? find_p(p->sid, n) : NULL;
        zxvd_buf_raw(r, ",\"session_owner\":", 17);
        if (lead)
            put_user(r, lead->uid);
        else
            zxvd_buf_raw(r, "null", 4);
        uint32_t job = zxvd_shell_job_of(p->pgid);
        zxvd_buf_printf(r, ",\"mine\":%s,\"job\":", p->uid == me ? "true" : "false");
        if (job)
            zxvd_buf_printf(r, "%u}", job);
        else
            zxvd_buf_raw(r, "null}", 5);
        shown++;
    }
    zxvd_buf_printf(r, "],\"count\":%u,\"truncated\":%s,\"control\":%s,\"restarts\":[", shown,
                    truncated ? "true" : "false", ON ? "true" : "false");
    const restart_t *order[ZXVD_PROC_RESTARTS]; /* oldest first */
    uint32_t nr = 0;
    for (uint32_t i = 0; i < ZXVD_PROC_RESTARTS; i++) {
        if (!R[i].id) continue;
        uint32_t at = nr++;
        while (at && order[at - 1]->id > R[i].id) {
            order[at] = order[at - 1];
            at--;
        }
        order[at] = &R[i];
    }
    for (uint32_t i = 0; i < nr; i++) {
        const restart_t *x = order[i];
        zxvd_buf_printf(r, "%s{\"restart\":%u,\"pid\":%ld,\"state\":\"%s\"", i ? "," : "", x->id,
                        x->old_pid,
                        x->state == R_STOPPING  ? "stopping"
                        : x->state == R_STARTED ? "started"
                                                : "failed");
        if (x->state == R_STARTED) zxvd_buf_printf(r, ",\"new_pid\":%ld", x->new_pid);
        if (x->state == R_FAILED) {
            zxvd_buf_raw(r, ",\"error\":", 9);
            zxvd_buf_json_str(r, x->error);
        }
        zxvd_buf_raw(r, "}", 1);
    }
    zxvd_buf_raw(r, "]}", 2);
}

/* P4: the process `pid` names, if zxvd may act on it */
static bool target(zxvd_call_t *c, long *pid, pinfo_t *p)
{
    uint32_t v = 0;
    if (!ON) {
        zxvd_fail(c, ZXVD_E_PROC, "process control is off in this zxvd");
        return false;
    }
    if (!zxvd_param_u32(c, "pid", &v, true)) return false;
    *pid = (long) v;
    if (v <= 1 || *pid == (long) getpid() || *pid == (long) getppid()) {
        zxvd_fail(c, ZXVD_E_PROC, "process %ld is not one zxvd acts on", *pid);
        return false;
    }
    if (!read_proc(*pid, p) || p->state == 'z') {
        zxvd_fail(c, ZXVD_E_PROC, "no process %ld", *pid);
        return false;
    }
    if (p->uid != (long) geteuid()) {
        zxvd_fail(c, ZXVD_E_PROC, "process %ld belongs to another account", *pid);
        return false;
    }
    return true;
}

static const struct {
    const char *name;
    int sig;
} SIGS[] = {{"term", SIGTERM}, {"kill", SIGKILL}, {"int", SIGINT},
            {"hup", SIGHUP},   {"stop", SIGSTOP}, {"cont", SIGCONT}};

void zxvd_proc_signal(zxvd_call_t *c)
{
    char name[8] = "term";
    long pid;
    pinfo_t p;
    if (!target(c, &pid, &p) || !zxvd_param_str(c, "signal", name, sizeof name, false)) return;
    if (!name[0]) memcpy(name, "term", 5);
    int sig = -1;
    for (size_t i = 0; i < sizeof SIGS / sizeof SIGS[0]; i++)
        if (!strcmp(name, SIGS[i].name)) sig = SIGS[i].sig;
    if (sig < 0) return zxvd_fail(c, E_PARAMS, "\"signal\" is term, kill, int, hup, stop or cont");
    if (kill((pid_t) pid, sig) != 0)
        return zxvd_fail(c, ZXVD_E_PROC, "the signal was not sent: %s", strerror(errno));
    zxvd_log(ZXVD_LOG_INFO, "proc", "sent %s to %ld", name, pid);
    zxvd_buf_printf(zxvd_result(c), "{\"pid\":%ld,\"signal\":\"%s\",\"sent\":true}", pid, name);
}

void zxvd_proc_restart(zxvd_call_t *c)
{
    long pid;
    pinfo_t p;
    uint32_t v = 0;
    if (ON && zxvd_param_u32(c, "pid", &v, true)) /* first: its old process may be gone */
        for (uint32_t i = 0; i < ZXVD_PROC_RESTARTS; i++)
            if (R[i].id && R[i].state == R_STOPPING && R[i].old_pid == (long) v)
                return zxvd_fail(c, ZXVD_E_PROC, "process %u is already restarting (%u)", v,
                                 R[i].id);
    if (!target(c, &pid, &p)) return;
    zxvd_proc_service();
    restart_t *r = NULL;
    for (uint32_t i = 0; i < ZXVD_PROC_RESTARTS && !r; i++)
        if (!R[i].id) r = &R[i];
    for (uint32_t i = 0; i < ZXVD_PROC_RESTARTS && (!r || r->id); i++) /* else the oldest ended */
        if (R[i].state != R_STOPPING && (!r || R[i].id < r->id)) r = &R[i];
    if (!r) return zxvd_fail(c, ZXVD_E_PROC, "%u restarts are under way", ZXVD_PROC_RESTARTS);
    restart_t n;
    memset(&n, 0, sizeof n);
    if (!(n.args = malloc(ZXVD_PROC_ARGV_MAX))) return zxvd_fail(c, ZXVD_E_PROC, "out of memory");
    size_t len = 0;
    if (!read_cmd(pid, n.exe, sizeof n.exe, n.args, &len, &n.argc, n.cwd, sizeof n.cwd)) {
        free(n.args);
        return zxvd_fail(c, ZXVD_E_PROC,
                         "process %ld's program, arguments or folder cannot be read", pid);
    }
    if (kill((pid_t) pid, SIGTERM) != 0) {
        int e = errno;
        free(n.args);
        return zxvd_fail(c, ZXVD_E_PROC, "the signal was not sent: %s", strerror(e));
    }
    r_free(r);
    *r = n;
    r->id = NEXT_R++;
    if (!NEXT_R) NEXT_R = 1;
    r->old_pid = pid;
    r->old_started_ms = p.started_ms;
    r->state = R_STOPPING;
    uint64_t now = mono_ms();
    r->kill_ms = now + 2000u;
    r->give_up_ms = now + 10000u;
    zxvd_log(ZXVD_LOG_INFO, "proc", "restart %u: stopping %ld", r->id, pid);
    zxvd_buf_printf(zxvd_result(c), "{\"restart\":%u,\"pid\":%ld,\"state\":\"stopping\"}", r->id,
                    pid);
}

#endif
