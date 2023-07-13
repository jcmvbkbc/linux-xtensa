#ifndef _XTENSA_TRAX_H
#define _XTENSA_TRAX_H
#include <asm/core.h>

#if defined(CONFIG_TRAX) && XCHAL_HAVE_TRAX

int trax_start(void);
int trax_stop(void);
void trax_dump(void);

#else

static inline int trax_start(void)
{
	return -EINVAL;
}

static inline int trax_stop(void)
{
	return -EINVAL;
}

static inline void trax_dump(void)
{
}

#endif

#endif
