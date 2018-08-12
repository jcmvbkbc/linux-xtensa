#include <linux/console.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/serial_core.h>
#include <linux/stddef.h>

#include <platform/simcall.h>

#include <asm/platform.h>

void __init platform_setup(char **cmdline)
{
}

/* early initialization */

void __init platform_init(bp_tag_t *first)
{
}
