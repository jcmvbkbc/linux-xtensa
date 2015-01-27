#
# Makefile for the Linux/Xtensa kernel.
#

extra-y := head.o vmlinux.lds

obj-y := coprocessor.o irq.o pci-dma.o platform.o process.o \
	 ptrace.o setup.o stacktrace.o syscall.o time.o

obj-$(CONFIG_XTENSA_XEA2) += align.o entry-xea2.o ptrace-xea2.o \
			     signal-xea2.o traps-xea2.o vectors-xea2.o
obj-$(CONFIG_XTENSA_XEA3) += entry-xea3.o ptrace-xea3.o \
			     signal-xea3.o traps-xea3.o vectors-xea3.o

obj-$(CONFIG_KGDB) += xtensa-stub.o
obj-$(CONFIG_PCI) += pci.o
obj-$(CONFIG_MODULES) += xtensa_ksyms.o module.o
obj-$(CONFIG_FUNCTION_TRACER) += mcount.o
obj-$(CONFIG_SMP) += smp.o mxhead.o

AFLAGS_head.o += -mtext-section-literals

# In the Xtensa architecture, assembly generates literals which must always
# precede the L32R instruction with a relative offset less than 256 kB.
# Therefore, the .text and .literal section must be combined in parenthesis
# in the linker script, such as: *(.literal .text).
#
# We need to post-process the generated vmlinux.lds scripts to convert
# *(xxx.text) to  *(xxx.literal xxx.text) for the following text sections:
#   .text .ref.text .*init.text .*exit.text .text.*
#
# Replicate rules in scripts/Makefile.build

sed-y = -e 's/\*(\(\.[a-z]*it\|\.ref\|\)\.text)/*(\1.literal \1.text)/g' \
	-e 's/\.text\.unlikely/.literal.unlikely .text.unlikely/g'	 \
	-e 's/\*(\(\.text\.[a-z]*\))/*(\1.literal \1)/g'

quiet_cmd__cpp_lds_S = LDS     $@
cmd__cpp_lds_S = $(CPP) $(cpp_flags) -P -C -Uxtensa -D__ASSEMBLY__ $<    \
                 | sed $(sed-y) >$@

vmlinux_lds_S-$(CONFIG_XTENSA_XEA2) = vmlinux-xea2.lds.S
vmlinux_lds_S-$(CONFIG_XTENSA_XEA3) = vmlinux-xea3.lds.S

$(obj)/vmlinux.lds: $(src)/$(vmlinux_lds_S-y) FORCE
	$(call if_changed_dep,_cpp_lds_S)
