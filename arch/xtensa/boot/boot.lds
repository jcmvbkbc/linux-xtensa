OUTPUT_ARCH(xtensa)
OUTPUT_FORMAT(elf32-xtensa-le)
ENTRY(_start)

SECTIONS
{
	.text :
	{
		_text_start = . ;
		*(.start)
		*(.literal)
		*(.rodata)
		*(.text)
		_text_end = . ;
	}

	.data ALIGN(0x04) :
	{
		*(.data)
		*(.data1)
		*(.sdata)
		*(.sdata2)
		*(.got.plt)
		*(.got)
		*(.dynamic)
	}

	__rela_dyn_start = .;
	.rela.dyn ALIGN(4)	: { *(.rela.dyn) }
	__rela_dyn_end = .;

	__bss_start = .;
	.bss			: { *(.bss) }
	__bss_end = .;

	. = ALIGN(0x10);
	__image_start = . ;
	.image : 
	{
		*(.image)
		. = (. + 3) & ~ 3;
	}
	__image_end = .	;

	_end = .;

	PROVIDE (end = .);
}
