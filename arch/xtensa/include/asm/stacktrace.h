#ifndef ASM_STACKTRACE_H
#define ASM_STACKTRACE_H

#include <linux/sched.h>

struct stackframe {
	unsigned long pc;
	unsigned long sp;
};

static __always_inline unsigned long *stack_pointer(struct task_struct *task)
{
	unsigned long *sp;

	if (!task || task == current)
		__asm__ __volatile__ ("mov %0, a1\n" : "=a"(sp));
	else
		sp = (unsigned long *)task->thread.sp;

	return sp;
}

void walk_stackframe(unsigned long *sp,
		int (*fn)(struct stackframe *frame, void *data),
		void *data);

#endif /* ASM_STACKTRACE_H */
