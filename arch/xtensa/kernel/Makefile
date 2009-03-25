#
# Makefile for the Linux/Xtensa kernel.
#

extra-y := head.o vmlinux.lds


obj-y := align.o entry.o irq.o coprocessor.o process.o ptrace.o \
	 setup.o signal.o syscall.o time.o traps.o vectors.o platform.o  \
	 pci-dma.o init_task.o io.o

#
# Allow literals to be inseted into text section.
# Need for MMU V3 code in mxhead.S
#
KBUILD_AFLAGS += -g3
AFLAGS_mxpic.o += -mtext-section-literals 

ifdef CONFIG_ARCH_HAS_SMP
obj-y += mxpic.o
else
obj-y += xtpic.o
endif

## windowspill.o

obj-$(CONFIG_KGDB) += kgdb.o
obj-$(CONFIG_PCI) += pci.o
obj-$(CONFIG_MODULES) += xtensa_ksyms.o module.o
obj-$(CONFIG_SMP) += smp.o mxhead.o


