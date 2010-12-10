#ifndef __LINUX_COMPILER_H
#error "Please don't include <linux/compiler-tensilica.h> directly, include <linux/compiler.h> instead."
#endif

#ifdef __XCC__
/* Some compiler specific definitions are overwritten here
 * for Intel XCC compiler
 */

#if __XCC_MINOR__ == 1
/* Boreal Release, Rev C,  doesn't suppport ' __attribute__((no_instrument_function)' */
#undef notrace
#define notrace
#endif 

#endif /* __XCC__ */
