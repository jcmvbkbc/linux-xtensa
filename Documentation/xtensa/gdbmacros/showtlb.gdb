###
###  EDIT THE FOLLOWING for your particular Xtensa/Diamond processor configuration.
###  FIXME: not tested with big-endian.
###
### Credits:
###       Marc Gauthier <marc@tensilica.com>
###

def set_pagination_off
#  set pagination off
end

def set_pagination_on
#  set pagination on
end

#  Memory management/protection type:
#	0 = Region Protection
#	1 = Region Protection with translation
#	2 = MMU T1040/T1050
#	3 = MMU RA/RB (MMU V2)
#	4 = MMU RC (MMU V3)
#
if xchal_have_ptp_mmu 
	if xchal_have_ptp_mmu && xchal_have_spanning_way
		set $mmutype = 4
	else
		set $mmutype = 3
	end
else
	set $mmutype = 2
end
#  Number of auto-refill way indices (4 or 8) if MMU:
set $iautosize = xchal_icache_ways
set $dautosize = xchal_dcache_ways
#  Endianness:
set $bigendian = xchal_have_be



#  Show data TLB state:
def dtshow
  printf "Dumping state of the entire DTLB\n"
  tlbshow 1 $dautosize
end
document dtshow
  Show the state of the data TLB (DTLB)
end

#  Show instruction TLB state:
def itshow
  printf "Dumping state of the entire ITLB\n"
  tlbshow 0 $iautosize
end
document itshow
  Show the state of the instruction TLB (ITLB)
end



#  Invoked by itshow and dtshow only.
#  Usage:  tlbshow  <isdata>  <autorefill_size>
def tlbshow
  set $_isdata   = $arg0
  set $_autosize = $arg1

  set_pagination_off
  if $mmutype == 0
    tlbshow_way $_isdata 0 3 29 0 0 ""
  end
  if $mmutype == 1
    tlbshow_way $_isdata 0 3 29 0 1 ""
  end
  if $mmutype >= 2
    set $iautolog2 = ($_autosize == 8 ? 3 : 2)
    tlbshow_way $_isdata 0 $iautolog2 12 8 1 "way 0 (4 kB)"
    tlbshow_way $_isdata 1 $iautolog2 12 8 1 "way 1 (4 kB)"
    tlbshow_way $_isdata 2 $iautolog2 12 8 1 "way 2 (4 kB)"
    tlbshow_way $_isdata 3 $iautolog2 12 8 1 "way 3 (4 kB)"
    set $_tlbcfg = ($_isdata ? $dtlbcfg : $itlbcfg)
    set $_varbits = (($_tlbcfg >> 16) & 3) * 2
    tlbshow_way $_isdata 4 2 (20+$_varbits) 8 1 "way 4 (%d MB wired)",(1<<$_varbits)
    if $mmutype < 4
      tlbshow_way $_isdata 5 1 27 8 1 "way 5 (128 MB static)"
      tlbshow_way $_isdata 6 1 28 8 1 "way 6 (256 MB static)"
    else
      set $_way5bits = 7 + (($_tlbcfg >> 20) & 1)
      set $_way6bits = 9 - (($_tlbcfg >> 24) & 1)
      tlbshow_way $_isdata 5 2 (20+$_way5bits) 8 1 "way 5 (%d MB wired)",(1<<$_way5bits)
      tlbshow_way $_isdata 6 3 (20+$_way6bits) 8 1 "way 6 (%d MB wired)",(1<<$_way6bits)
    end
    if $_isdata
      tlbshow_way 1 7 0 12 8 1 "way 7 (4 kB wired)"
      tlbshow_way 1 8 0 12 8 1 "way 8 (4 kB wired)"
      tlbshow_way 1 9 0 12 8 1 "way 9 (4 kB wired)"
    end
  end
  set_pagination_on
end


#  Invoked by tlbshow only.
#  Usage:  tlbshow_way  <isdata>  <way_number>  <num_entries_log2>  <pgsz_bits>  <asid_bits>  <xlate>  <msg>
def tlbshow_way
  set $_isdata    = $arg0
  set $_waynum    = $arg1
  set $_n_log2    = $arg2
  set $_pgsz_bits = $arg3
  set $_asid_bits = $arg4
  set $_xlate     = $arg5

  printf "Showing way %d\n", $_waynum
  set $_n = (1 << $_n_log2)
  set $_pgsz = (1 << $_pgsz_bits)
  set $_vpn_bits = (32 - ($_n_log2 + $_pgsz_bits))

  set $savea6 = $a6
  set $savea8 = $a8
  set $savea9 = $a9

  set $_i = 0
  while $_i < $_n

    #  NOTE: can't use a2,a3,a4 (used by OCD daemon itself)

    set $_vaddr = ($_i * $_pgsz)
    set $a9 = $_vaddr + $_waynum

    #  Read VPN and ASID:
    if $_asid_bits > 0 || $_vpn_bits > 0
      # FIXME: not tested with big-endian
      if $_isdata
	#  RDTLB0 a6, a9
	if $bigendian
	  monitor exe 069b05
	else
	  monitor exe 60b950
	end
      else
	#  RITLB0 a6, a9
	if $bigendian
	  monitor exe 069305
	else
	  monitor exe 603950
	end
      end
    end

    #  Flush register cache so we can see register results:
    flushregs

    #  if enabled Skip display for invalid ASIDs:
    if 1 || $_asid_bits == 0 || ($a6 & 0xFF) != 0

      #  Read PPN and CA:
      # FIXME: not tested with big-endian
      if $_isdata
	#  RDTLB1 a8, a9
	if $bigendian
	  monitor exe 089f05
	else
	  monitor exe 80f950
	end
      else
	#  RITLB1 a8, a9
	if $bigendian
	  monitor exe 089705
	else
	  monitor exe 807950
	end
      end

    #  Flush register cache so we can see register results:
    flushregs

      #  Compute full vaddr:
      if $_vpn_bits > 0
	set $_vaddr = $_vaddr + ($a6 & -($_n * $_pgsz))
      end

      #  Display   vaddr [asid] [paddr] ca
      printf "vaddr=0x%08x", $_vaddr
      if $_asid_bits > 0
	printf " asid=0x%02x", ($a6 & 0xFF)
      end
      if $_xlate
	printf "  paddr=0x%08x", ($a8 & -$_pgsz)
      end
      printf "  ca=%d  ", ($a8 & 15)
      if $_isdata
	printf "DTLB "
      else
	printf "ITLB "
      end
      printf $arg6
      printf "\n"

    end

    set $_i = $_i + 1
  end

  set $a6 = $savea6
  set $a8 = $savea8
  set $a9 = $savea9
end

