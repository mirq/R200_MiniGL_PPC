ENTRY(_start)

SECTIONS
{
  .text 0 : { _start = .; *(.text .text.* .gnu.linkonce.t.*) }
  .init 0 : { *(.init .init.*) }
  .fini 0 : { *(.fini .fini.*) }
  .rodata 0 : { *(.rodata .rodata.* .gnu.linkonce.r.*) }
  .ctors 0 : { __ctrslist = .; *(.ctors .ctors.*) }
  .dtors 0 : { __dtrslist = .; *(.dtors .dtors.*) }
  .data 0 : { *(.data .data.* .gnu.linkonce.d.*) }
  .sdata 0 : { *(.sdata .sdata.* .gnu.linkonce.s.*) }
  .sbss 0 (NOLOAD) : { *(.sbss .sbss.* .gnu.linkonce.sb.*) *(.scommon) }
  .bss 0 (NOLOAD) : { *(.bss .bss.* .gnu.linkonce.b.*) *(COMMON) }
  /DISCARD/ : { *(.eh_frame .eh_frame.*) *(.comment .comment.*) *(.gnu.attributes) *(.note.GNU-stack) }
}
