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

#
# In the Xtensa architecture, assembly generates literals which must always
# precede the code (the L32R instruction that loads them only uses negative
# PC-relative offsets).  For any *.text section, literals are placed in a
# corresponding *.literal section.  The linker script (vmlinux.lds) must
# place these in the correct order.  It must also combine them, when the
# *.text section can be larger than L32R's 256 kB range.
#
# For example, this doesn't work:
#	*(.literal) *(.text)
# because L32R instructions at the end of .text can't reach the literals.
#
# The linker can solve this if they are combined in parentheses, like this:
#	*(.literal .text)
# because it is now allowed mix literals in .text to bring them in range.
#
# None of this is done by standard vmlinux.lds.h macros such as TEXT_TEXT
# and INIT_TEXT.  To avoid replicating the logic of that header file, we
# instead post-process the generated linker script to convert *(xxx.text)
# to *(xxx.literal xxx.text) for the following text sections:
#	.text .ref.text .*init.text .*exit.text .text.*
# using a sed script.  To do this we must override the default rule for
# vmlinux.lds (see scripts/Makefile.build and the top-level Makefile)
# to insert this extra step.
#
export CPPFLAGS_vmlinux.lds.in += -P -C -U$(ARCH)
$(obj)/vmlinux.lds: $(obj)/vmlinux.lds.in
	@sed -e 's/(\(\.[a-z]*it\|\.ref\|\)\.text)/(\1.literal \1.text)/g' \
	     -e 's/(\(\.text\.[a-z]*\))/(\1.literal \1)/g' $< > $@
$(obj)/vmlinux.lds.in: $(src)/vmlinux.lds.in.S
	$(call if_changed_dep,cpp_lds_S)

## windowspill.o

obj-$(CONFIG_KGDB) += kgdb.o
obj-$(CONFIG_PCI) += pci.o
obj-$(CONFIG_MODULES) += xtensa_ksyms.o module.o
obj-$(CONFIG_SMP) += smp.o mxhead.o


