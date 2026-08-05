/* hello.c — a real ZXV user application (AArch64 EL0)
 *
 * Compiled to a static ELF64, linked at 0x10000, and loaded from the
 * persistent ZXVFS by the kernel's ELF loader — NOT copied from a
 * kernel function. Uses only SVC syscalls; no libc, no kernel symbols.
 *
 * Proves: an application authored, compiled, and stored as an ELF file
 * on disk is loaded into an isolated EL0 address space and executed.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV ELF-loader slice)
 * License: SEL-3.3
 */

#define SYS_EXIT   1
#define SYS_WRITE  2
#define SYS_GETPID 3
#define SYS_EXEC   11

__attribute__((always_inline))
static inline void sys_write(char c) {
    register long x8 __asm__("x8") = SYS_WRITE;
    register long x0 __asm__("x0") = c;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
}

__attribute__((always_inline))
static inline long sys_getpid(void) {
    register long x8 __asm__("x8") = SYS_GETPID;
    register long x0 __asm__("x0");
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    return x0;
}

__attribute__((always_inline))
static inline void sys_exit(int code) {
    register long x8 __asm__("x8") = SYS_EXIT;
    register long x0 __asm__("x0") = code;
    __asm__ volatile("svc #0" : : "r"(x8), "r"(x0) : "memory");
}

/* Attempt a privileged syscall this app has no capability for. */
__attribute__((always_inline))
static inline long sys_exec(const char *buf, long len) {
    register long x8 __asm__("x8") = SYS_EXEC;
    register long x0 __asm__("x0") = (long)buf;
    register long x1 __asm__("x1") = len;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1) : "memory");
    return x0;
}

static void puts_(const char *s) {
    for (const char *p = s; *p; p++)
        sys_write(*p);
}

/* Entry point (linker ENTRY(_start)). */
void _start(void) {
    puts_("\r\n");
    puts_("+--------------------------------------------------+\r\n");
    puts_("|  Hello from a REAL ELF, loaded off ZXVFS disk.   |\r\n");
    puts_("|  Running at EL0 in its own address space.        |\r\n");
    puts_("+--------------------------------------------------+\r\n");

    long pid = sys_getpid();
    puts_("my pid = ");
    /* print pid as a single digit (it is small) */
    sys_write((char)('0' + (pid % 10)));
    puts_("\r\n");

    /* Least-privilege proof: this app has CAP_WRITE|CAP_PROC only, so an
     * attempt to run a shell command via SYS_EXEC must be denied (-EPERM
     * = -3) by the kernel capability gate. */
    static const char cmd[] = "stats";
    long rc = sys_exec(cmd, 5);
    if (rc == -3)
        puts_("privileged SYS_EXEC correctly DENIED (least privilege ok)\r\n");
    else
        puts_("WARNING: SYS_EXEC was NOT denied — capability gate FAILED\r\n");

    sys_exit(0);
    for (;;) { }
}
