###
### Credits:
###       Marc Gauthier <marc@tensilica.com>
###	  Pete Delaney <piet@tensilica.com>
###
###  Shows memory PTE's, also provids some usefull macros 
###  for getting the current task pointer
###

define show_mem_pte
    set $vaddr = (unsigned long) $arg0
    if ($argc > 1)
   	set $task = (struct task_struct *) $arg1
    else
	get_current
	set $task = $current
    end	

    printf "Display pte for task:0x%08x, vaddr:0x%08x\n", $task, $vaddr 

    set $vaddrpdo = (($vaddr >> 22) & 0x3FF)*4
    set $vaddrpto = (($vaddr >> 12) & 0x3FF)*4
    set $vaddrofs = ($vaddr & 0xFFF)

    set $taskmm = $task->mm
    if $taskmm != 0
    	printf "$taskmm = task->mm:0x%08x\n", $taskmm
    else	
	set $taskmm = $task->active_mm
    	printf "$taskmm = task->active_mm:0x%08x\n", $taskmm
    end

    set $gpd = (unsigned)$taskmm->pgd
    printf "Task pgd is 0x%08x+%X\n", $gpd, $vaddrpdo

    set $pgt = *(unsigned*)($gpd + $vaddrpdo)
    printf "task:0x%08x, pgd:0x%08x+0x%x, pgt:0x%08x+0x%x\n", $task, $gpd, $vaddrpdo, $pgt, $vaddrpto

    set $pte = *(unsigned*)($pgt + $vaddrpto)
    set $paddr = ($pte & 0xFFFFF000) + $vaddrofs
    printf "Vaddr:0x%08x maps to Paddr:0x%08x pte:0x%08x.lsb:0x%03x\n", $vaddr, $paddr, $pte, ($pte & 0xFFF)
end
document show_mem_pte
  Show the physical memory copy of PTE entry for the specified virtual address.
  Usage: show_mem_pte <virtual_address> [ <task-pointer> ]
end

define get_current
    if $argc > 0
	set $stackptr = $arg0
    else
	set $stackptr = $sp
    end
    set $stackmask = ~(thread_size - 1)
    set $taskbase = ($stackptr & $stackmask)
    set $tinfo = (struct thread_info*)$taskbase	
    set $current = $tinfo->task
    printf "$current = 0x%x\n", $current
end
document get_current
  Get current task pointer
  Usage: get_current [optional-stack-pointer]
end

define current
    if $argc > 0
	get_current $arg0
    else
        get_current
    end
end
document current
  Shorthand for get_current
  Usage: current [optional-stack-pointer]
end

define vmap
    if $argc > 1
	get_current $arg1
    else
        get_current
    end

    show_mem_pte $arg0 $current
end
document vmap
  Show the physical memory copy of PTE entry for the specified virtual address.
  Usage: vmap <virtual_address> [<stack-pointer>]
end

