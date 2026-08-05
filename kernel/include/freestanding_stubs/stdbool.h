/* freestanding_stubs/stdbool.h — Minimal <stdbool.h> stub for freestanding builds
 *
 * Maps the bool/true/false macros to a consistent representation.
 * In freestanding mode we use a typedef'd _Bool alias.
 */
#ifndef FREESTANDING_STDBOOL_H
#define FREESTANDING_STDBOOL_H

#define bool _Bool
#define true 1
#define false 0
#define __bool_true_false_are_defined 1

#endif
