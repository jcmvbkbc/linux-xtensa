#
# Macros for Displaying the Incstruction and Data Caches
#   'icshow' - Instruction Cache Show
#   'dcshow' - Data Cache Show
#
# NOTE:
#	Uses the xt-gdb 'monitor exe' facility to execute 
#	instruction in the OCD daemon.
#
# Credits:
#       Marc Gauthier <marc@tensilica.com>

set $keyval = 0

#  Show instruction cache state:
def icshow
  set $keyval = 0x1CAC4E
  ###
  ###  EDIT THE FOLLOWING for your particular Xtensa/Diamond processor configuration.
  ###  FIXME: not tested with big-endian.
  ###
  set $isize = xchal_icache_size
  set $ilinesize = xchal_icache_linesize;
  set $iways = xchal_icache_ways
  set $ilockable = xchal_icache_line_lockable
  set $bigendian = xchal_have_be
  icacheshow
end
document icshow
  Show the state of the instruction cache (icache)
end

#  Show data cache state:
def dcshow
  set $keyval = 0xDCAC4E
  ###
  ###  EDIT THE FOLLOWING for your particular Xtensa/Diamond processor configuration.
  ###  FIXME: not tested with big-endian.
  ###
  set $dsize = xchal_dcache_size
  set $dlinesize = xchal_dcache_linesize
  set $dways = xchal_dcache_ways
  set $dlockable = xchal_dcache_line_lockable
  set $dwriteback = xchal_dcache_is_writeback
  set $bigendian = xchal_have_be
  #  Set this to 1 for MMU with TLBs/autorefill, set to 0 for region protection or XEA1:
  set $fullmmu = xchal_have_ptp_mmu
  dcacheshow
end
document dcshow
  Show the state of the data cache (dcache)
end


#  Invoked by icshow macro only.  DO NOT INVOKE DIRECTLY
def icacheshow
  if $keyval != 0x1CAC4E
    printf "ERROR: don't invoke this command directly, use  icshow  instead.\n"
    #  Exit macro by error:
    void void void
  end
  set $keyval = 0
  set pagination off
  printf "Dumping state of I-cache (%d kB, %d-way assoc, %d bytes per line)\n", $isize, $iways, $ilinesize
  set $waysize = $isize / $iways

  set $savea8 = $a8
  set $savea9 = $a9

  set $i = 0
  while $i < $waysize

    set $way = 0
    while $way < $iways

      #  NOTE: can't use a2,a3,a4 (used by OCD daemon itself)

      set $a9 = $i + ($way * $waysize)

      # Execute Load Instruction Cache Tag (LICT) 
      #	Instruction in on CPU via JTAG
      #    LICT a8, a9
      if $bigendian
	#   FIXME: not tested with big-endian
	monitor exe 08901f
      else
	monitor exe 8009f1
      end
      flushregs

      #  If valid cache tag in a2, display it:
      #printf "Got 0x%08x for 0x%x\n", $a8, $a9
      if ($a8 & 1) != 0
	set $paddr = ($a8 & -$waysize) + $i
	set $tag = ($a8 & ($waysize - 1))
	printf "paddr 0x%08x, way %d", $paddr, $way
	if $iways > 1
	  set $tag = ($tag >> 1)
	  #  refill way select bit:
	  printf ", F=%d", ($tag & 1)
	end
	if $ilockable
	  set $tag = ($tag >> 1)
	  #  line lock bit:
	  printf ", L=%d", ($tag & 1)
	end
	printf ": "
	#  Display cache words:
	set $n = 0
	while $n < $ilinesize
	  #  LICW a8, a9
	  if $bigendian
	    # FIXME: not tested with big-endian
	    monitor exe 08921f
	  else
	    monitor exe 8029f1
	  end
	  flushregs
	  if $bigendian
	    printf " %08x", $a8
	  else
	    printf " %02x%02x%02x%02x", ($a8&0xFF), (($a8>>8)&0xFF), (($a8>>16)&0xFF), (($a8 >> 24)&0xFF)
	  end
	  set $a9 = $a9 + 4
	  set $n = $n + 4
	end
	printf "\n"
      end

      set $way = $way + 1
    end

    set $i = $i + $ilinesize
  end

  set $a8 = $savea8
  set $a9 = $savea9
  set pagination on
end

#  Invoked by dcshow macro only.  DO NOT INVOKE DIRECTLY
def dcacheshow
  if $keyval != 0xDCAC4E
    printf "ERROR: don't invoke this command directly, use  dcshow  instead.\n"
    #  Exit macro by error:
    void void void
  end
  set $keyval = 0
  set pagination off
  printf "Dumping State of D-cache (%d kB, %d-way assoc, %d bytes per line)\n", $dsize, $dways, $dlinesize
  set $waysize = $dsize / $dways
  set $taggedsize = $waysize
  printf "taggedsize = %d\n", $taggedsize
  set $pagesize = 4096
  set $pagesizelog2 = 12
  if ($fullmmu && ($waysize > $pagesize))
    set $taggedsize = $pagesize
  end

  printf "taggedsize = %d\n", $taggedsize

  set $savea8 = $a8
  set $savea9 = $a9

  set $i = 0
  while $i < $waysize

    set $way = 0
    while $way < $dways

      #  NOTE: can't use a2,a3,a4 (used by OCD daemon itself)

      set $a9 = $i + ($way * $waysize)
      #  LDCT a8, a9
      if $bigendian
	# FIXME: not tested with big-endian
	monitor exe 08981f
      else
	monitor exe 8089f1
      end
      flushregs

      #  If valid cache tag in a2, display it:
      #printf "Got 0x%08x for 0x%x\n", $a8, $a9
      if ($a8 & 1) != 0
	set $paddr = ($a8 & -$taggedsize) + ($i & ($taggedsize - 1))
	set $tag = ($a8 & ($waysize - 1))
	printf "paddr 0x%08x", $paddr
	if $waysize != $taggedsize
	  #  show virtual index
	  printf ", vin %d", ($i >> $pagesizelog2)
	end
	printf ", way %d", $way
	if $dways > 1
	  set $tag = ($tag >> 1)
	  #  refill way select bit:
	  printf ", F=%d", ($tag & 1)
	end
	if $dwriteback
	  set $tag = ($tag >> 1)
	  #  line dirty bit:
	  printf ", D=%d", ($tag & 1)
	end
	if $dlockable
	  set $tag = ($tag >> 1)
	  #  line lock bit:
	  printf ", L=%d", ($tag & 1)
	end
	printf ": "
	#  Display cache words:
	#  Need to convert paddr to vaddr, if we can.  Right now just hardcode MMU case:
	if $fullmmu
	  if $paddr < 0x08000000
	    #  KSEG area:
	    set $vaddr = $paddr + 0xD0000000
	  else
	    if $paddr >= 0xF0000000
	      #  KIO area:
	      set $vaddr = $paddr - 0x10000000
	    else
	      #  Anywhere else:
	      set $vaddr = -1
	    end
	  end
	else
	  set $vaddr = $paddr
	end
	set $n = 0
	while $n < $dlinesize
	  if $vaddr == -1
	    printf " ????????"
	  else
	    set $dw = *(unsigned int*)($vaddr + $n)
	    if $bigendian
	      printf " %08x", $dw
	    else
	      printf " %02x%02x%02x%02x", ($dw&0xFF), (($dw>>8)&0xFF), (($dw>>16)&0xFF), (($dw >> 24)&0xFF)
	    end
	  end
	  set $n = $n + 4
	end
	printf "\n"
      end

      set $way = $way + 1
    end

    set $i = $i + $dlinesize
  end

  set $a8 = $savea8
  set $a9 = $savea9
  set pagination on
end

