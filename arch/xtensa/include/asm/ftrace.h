#ifndef _XTENSA_FTRACE_H
#define _XTENSA_FTRACE_H

#include <asm/processor.h>

#define HAVE_ARCH_CALLER_ADDR
#ifdef CONFIG_FRAME_POINTER
extern unsigned long return_address(unsigned level);
#define CALLER_ADDR0 return_address(0)
#define CALLER_ADDR1 return_address(1)
#define CALLER_ADDR2 return_address(2)
#define CALLER_ADDR3 return_address(3)
#else
#define CALLER_ADDR0 ({ unsigned long a0, a1; \
	__asm__ __volatile__ ("mov %0, a0 ; mov %1, a1\n" : "=r"(a0), "=r"(a1) : : ); \
	 MAKE_PC_FROM_RA(a0, a1); })
#define CALLER_ADDR1 (0)
#define CALLER_ADDR2 (0)
#define CALLER_ADDR3 (0)
#endif

#endif /* _XTENSA_FTRACE_H */
