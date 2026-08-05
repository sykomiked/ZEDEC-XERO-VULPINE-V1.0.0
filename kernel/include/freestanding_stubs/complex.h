/* freestanding complex.h — Minimal complex number support for freestanding kernel
 * In -std=c11 -ffreestanding mode, 'complex' is not a keyword unless this header
 * is included. We define it as _Complex which is the GCC builtin.
 */
#ifndef FREESTANDING_COMPLEX_H
#define FREESTANDING_COMPLEX_H

#define complex _Complex
#define _Complex_I (__extension__ 1.0iF)
#define I _Complex_I

static inline double creal_fc(double _Complex z) { return __builtin_creal(z); }
static inline double cimag_fc(double _Complex z) { return __builtin_cimag(z); }
static inline double cabs_fc(double _Complex z) { return fs_cabs(z); }
static inline double _Complex conj_fc(double _Complex z) { return __builtin_conj(z); }
static inline double _Complex cexp_fc(double _Complex z) { return fs_cexp(z); }

#define creal creal_fc
#define cimag cimag_fc
#define cabs cabs_fc
#define conj conj_fc
#define cexp cexp_fc

#endif
