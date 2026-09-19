#ifndef ZXV_BARRIER_H
#define ZXV_BARRIER_H
/* One guarantee, five spellings.
 *
 * A full system memory barrier is named differently by every ISA. Modules
 * state the EFFECT they need; the architecture supplies the mnemonic. This is
 * the same rule the rest of the kernel follows: no fixed values, adapt to the
 * hardware actually underneath. Hardcoding "dsb sy" made a device driver
 * arm64-only for no reason other than spelling.
 */
#if defined(__aarch64__)
#  define ZXV_DSB() __asm__ volatile("dsb sy" ::: "memory")
#elif defined(__arm__)
#  define ZXV_DSB() __asm__ volatile("dsb" ::: "memory")
#elif defined(__riscv)
#  define ZXV_DSB() __asm__ volatile("fence iorw,iorw" ::: "memory")
#elif defined(__x86_64__) || defined(__i386__)
#  define ZXV_DSB() __asm__ volatile("mfence" ::: "memory")
#else
#  define ZXV_DSB() __asm__ volatile("" ::: "memory")
#endif
#endif /* ZXV_BARRIER_H */
