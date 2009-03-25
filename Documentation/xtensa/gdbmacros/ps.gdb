#
# This file contains macros for walking through the list of task
# and performing various operations:
#
#	'ps'                - Printing a 'ps' like brief stmmary of the task
#	'btt'               - Printing a backtrace for all task (Similar to Kdump btt macro)
#	'btpid'             - Print the backtrace for a partifular task
#	'btn'               - Print the backtrace for the next task
#	'btp'               - Print the backtrace for the previous task
#	'switch'            - Switching to the stack environment for a particular task
#	'resume'            - Switching back (resume) to the origianl task
#	'save_state'        - Save current (Linux) State; done automatically when $ccount changes.
#	'check_state'       - Compare current state to Saved (Linux) State; do before doing a 'continue'.
#	'print_state'       - Print the current state
#	'print_saved_state' - Print the current saved (Linux) state
#
#
# It's designed to even run with xt-gdb being driven with ddd and being able to
# easily walk up and down the stack and displaying local and formal parameters.
# We reciently got to almost a week stability and a task seems to be holding a 
# resource that the other task are waiting on. Needed to be able to see the stacks 
# of all task prior to KGDB working.
#
# Credits:
#	Piet Delaney <piet@tensilica.com>
#	Marc Gauthier <marc@tensilica.com>
#
#
# Global gdb Variables:
#	$_target_pid - looking only for this pid
#	$_print_ps   - print ps info while looking at tasks
#	$_print_bt   - print a backtrace while looking at task
#	$_verbose    - debug verbosity:
#		       0: 0ff
#                      1: Function Entrys and Exits
#		       2: Details of Register Being changed
#
#	$_saved_* 0  - Saved registers of current Linux task that gdb stopped in
#	$bt_saved_*  - Saved registers at entry to loop looking at task list
#	$current     - Current task we are giving backtrace for.
#
#
#	While switching to another stack you have to be rather carefull 
#	with maintaining stack consistancy. gdb is rather unforgiving
#	and doesn't allow 'set backtrace limit' to examine 0 frames.
#	We get around this by switching to the non-windowed ABI while
#	changing the state; see disable_bt(). For the task that are not
#	running we use the windowed ABI to do the back-trace. 
#
#	The current running process is special, we can display the stack
#	but it didn't go through switch, so we can't change the window
#	parameters. For this context we restore the Saved Linux state.
#
# NOTE:
#	$ptevadder is getting whacked by OCD but can't be preserved here.
#

set var $_saved_ccount = 0
set pagination off

define version
	printf "Thu Mar 19 11:45pm\n"
end

define switch 
	set var $_target_pid = $arg0
	set var $_print_ps = 0
	set var $_print_bt = 1
	set var $_verbose = 0
	for_each_task
	if ($current != 0)
		save_state
		switch_state $current
	end	
end
document switch
  Switch backtrace context to 'pid'
end


define btt
	save_state
	set var $_target_pid = -1
	set var $_print_ps = 1
	set var $_print_bt = 1
	set var $_verbose = 0
	for_each_task
end
document btt
  Backtrace all task
end


define btpid
	save_state
	set var $_target_pid = $arg0
	set var $_print_ps = 0
	set var $_print_bt = 1
	set var $_verbose = 0
	for_each_task
end
document btpid
  Backtrace a particular 'pid'
end

define btn
	set var $_target_pid = $next_tsk->pid
	set var $_print_ps = 0
	set var $_print_bt = 1
	set var $_verbose = 0
	for_each_task
	if ($current != 0)
		save_state
		switch_state $current
	end
end
document btn
  Backtrace next task
end

define btp
	set var $_target_pid = $prev_tsk->pid
	set var $_print_ps = 0
	set var $_print_bt = 1
	set var $_verbose = 0
	for_each_task
	if ($current != 0)
		state_state
		switch_state $current
	end
end
document btp
  Backtrace previous task
end

define ps
	save_state
	set var $_target_pid = -1
	set var $_print_ps = 1
	set var $_print_bt = 0
	set var $_verbose = 0
	for_each_task
end
document ps
  Show the process/task state
end


define resume
	set var $_verbose = 0
	restore_state
end
document resume
  Restore current context back from Switch
end


define save_state
	if ($_saved_ccount != $ccount)
		printf "save_state: _saved_ccount:0x%x != $ccount:0x%x; Saving Current Linux State\n", $_saved_ccount, $ccount
		set var $_saved_pc = $pc
		set var $_saved_pc = $pc
		set var $_saved_a0 = $a0
		set var $_saved_a1 = $a1
		set var $_saved_sp = $sp
		set var $_saved_wb = $windowbase
		set var $_saved_ws = $windowstart
		set var $_saved_ps = $ps
		set var $_saved_ccount = $ccount
		set var $_saved_ptevaddr = $ptevaddr
		set var $_saved_state_valid = 1

		print_saved_state
	end
end
document save_state
  Internal Function to save the state prior to a switch to another task stack.
end

define print_state
		printf " $pc: 0x%x\n",  $pc
		printf " $a0: 0x%x\n",  $a0
		printf " $a1: 0x%x\n",  $a1
		printf " $sp: 0x%x\n",  $sp
		printf " $windowbase: 0x%x\n",  $windowbase
		printf " $windowstart: 0x%x\n",  $windowstart
		printf " $ps: 0x%x\n",  $ps
		printf " $ccount: 0x%x\n",  $ccount
		printf " $ptevaddr: 0x%x\n",  $ptevaddr
		printf "\n"
end
document print_state
  Print current state
end
	

define print_saved_state
	if $_saved_state_valid
		printf " $_saved_pc: 0x%x\n",  $_saved_pc
		printf " $_saved_a0: 0x%x\n",  $_saved_a0
		printf " $_saved_a1: 0x%x\n",  $_saved_a1
		printf " $_saved_sp: 0x%x\n",  $_saved_sp
		printf " $_saved_wb: 0x%x\n",  $_saved_wb
		printf " $_saved_ws: 0x%x\n",  $_saved_ws
		printf " $_saved_ps: 0x%x\n",  $_saved_ps
		printf " $_saved_ccount: 0x%x\n",  $_saved_ccount
		printf " $_saved_ptevaddr: 0x%x\n",  $_saved_ptevaddr
		printf "\n"
	else
		printf "State Not Saved Yet\n"
	end
end
document print_saved_state
  Usefull for debugging this script
end
	
define check_state
	if $_saved_state_valid
		if $_saved_pc != $pc
			printf " $_saved_pc:0x%x != $pc:0x%x\n",  $_saved_pc, $pc
		end

		if $_saved_a0 != $a0
			printf " $_saved_a0:0x%x != $a0:0x%x\n",  $_saved_a0, $a0
		end

		if $_saved_a1 != $a1
			printf " $_saved_a1:0x%x != $a1:0x%x\n",  $_saved_a1, $a1
		end

		if $_saved_sp != $sp
			printf " $_saved_sp:0x%x != $sp:0x%x\n",  $_saved_sp, $sp
		end

		if $_saved_wb != $windowbase
			printf " $_saved_wb:0x%x != $windowbase:0x%x\n",  $_saved_wb, $windowbase
		end

		if $_saved_ws != $windowstart
			printf " $_saved_ws:0x%x != $windowstart:0x%x\n",  $_saved_ws, $windowstart
		end

		if $_saved_ps != $ps
			printf " $_saved_sp:0x%x != $sp:0x%x\n",  $_saved_sp, $sp
		end

		if $_saved_ccount != $ccount
			printf " $_saved_ccount:0x%x != $ccount:0x%x\n",  $_saved_ccount, $ccount
		end

		if $_saved_ps != $ps
			printf " $_saved_ptevaad:0x%x != $ptevaad:0x%x\n",  $_saved_ptevaad, $ptevaad
		end

		printf "\n"
	else
		printf "State Not Saved Yet\n"
	end
end
document check_state
  Compares current state to saved state
end
	


define restore_state
	if $_verbose
		printf "restore_state {\n"
	end

	frame 0

 	disable_bt

	set $restore_state_psexcm = $psexcm
	set $psexcm = 1
   	set $pc = $_saved_pc
   	set $a0 = $_saved_a0
       	set $a1 = $_saved_a1
        set $ptevaddr = $_saved_ptevaddr
        set $windowbase = $_saved_wb
        set $windowstart = $_saved_ws
	set $psexcm = $restore_state_psexcm

	enable_bt

	set $ps = $_saved_ps
	
	if ( $sp != $_saved_sp )
		printf "restore_state: ERROR - $sp:0x%x != $_saved_sp:0x%x\n", $sp, $_saved_sp
	end

	if $_verbose
		 printf "restore_state }\n"
	end
end
document restore_state
  Internal Function to restore state after a switch to another task stack.
end

define disable_bt
	if $_verbose
		printf "    disable_bt {\n"
		printf "      $ps:0x%x, $psexcm:0x%x, $pswoe:0x%x\n",  $ps, $psexcm, $pswoe
	end

	
	set backtrace limit 1
	set var $saved_psexcm  = $psexcm
	set var $saved_pswoe   = $pswoe

	if ($saved_psexcm == 0)
		if $_verbose > 1
			printf "      set $psexcm = 1\n"
		end
		set $psexcm = 1
	end

	if ($saved_pswoe == 1)
		if $_verbose > 1
			printf "      set $pswoe = 0\n"
		end
		set $pswoe = 0
	end

	if $_verbose
		printf "    disable_bt }\n"
	end
end
document disable_bt
  Internal Function to disable gdb from trying to do a backtrace while we are changing the stack
end

define enable_bt
	if $_verbose
		printf "    enable_bt {\n"
	end

	if ( $saved_psexcm != $psexcm )
		if $_verbose > 1
			printf "      set $psexcm = $saved_psexcm:%x\n", $saved_psexcm, 
		end
		set $psexcm = $saved_psexcm
	end

	if ( $saved_pswoe != $pswoe )
		if $_verbose > 1
			printf "      set $pswoe = $saved_pswoe:0x%x\n", $saved_pswoe
		end
		set $pswoe = $saved_pswoe
	end

	set backtrace limit 0

	if $_verbose
		printf "    enable_bt }\n"
	end
end
document enable_bt
  Internal Function to disable gdb from trying to do a backtrace while we are changing the stack
end


#
# NOTE:
#       From include/asm-xtensa/thread_info.h	    /* Used in switch_state() macro below ... */
#	    #define TIF_CURRENTLY_RUNNING   17      /* ... True if thread is currently running on ...*/
#						    /* ... it's CPU; Added for supporting this gdb macro */
#
# Returns via globals:
#	$switchable
#	$running
#	$cpu
#
define switch_state
	set var $new_tsk = $arg0
	set var $ti = (struct thread_info *) $new_tsk->stack
	set var $thread = (struct thread_struct *) &$new_tsk.thread
	set var $cpu = $ti->cpu
	set var $flags = $ti->flags
	set var $status = $ti->status
	set var $running = $flags & (1 << 17)
	set var $switchable = (($running == 0) || ($cpu == $prid))

	if $_verbose
		printf "switch_state($new_tsk:%x) { switchable:%d", $new_tsk, $switchable
		printf "  $windowbase:%x, $windowstart:%x, $a0:%x, $sp:%x, $ps:%x\n", $windowbase, $windowstart, $a0, $sp, $ps
	end

	if ($switchable) 	
		disable_bt

		if $_verbose > 1
			 printf "  set $windowstart = (1 <<  $windowbase:%d):0x%x\n", $windowbase, (1 << $windowbase)
		end
		set $windowstart = (1 << $windowbase)
	
		if $_verbose > 1
			printf "  set $a0 = $thread->ra:0x%x\n", $thread->ra
		end
		set $a0 = $thread->ra
	
		if $_verbose > 1
			 printf "  set $sp = $thread->sp:0x%x\n", $thread->sp
		end
		set $sp = $thread->sp
	
		if ($running == 0) 
			if ($_verbose > 1)
				printf "  set $pc = &switch_to:0x%x + 3\n", &_switch_to
			end
			set $pc = (&_switch_to + 3)
		end
		enable_bt
	end

#	flush
#	flushregs

	if $_verbose 
		printf "switch_state }\n"
	end
end
document switch_state
  Internal Function to switch stack to task for 'pid'.
end


define save_bt_state
	if $_verbose
		printf "  save_bt_state: {\n"
		printf "    $a1:0x%x, $ps:0x%x, $ptevadder:0x%x, $sp:0x%x, $a0:0x%x, $windowbase:0x%x, $windowstart:0x%x\n", $a1, $ps, $ptevaddr, $sp, $a0, $windowbase, $windowstart
	end
	frame 0
	set var $bt_saved_pc = $pc
	set var $bt_saved_ps = $ps
	set var $bt_saved_ptevaddr = $ptevaddr
	set var $bt_saved_sp = $sp
	set var $bt_saved_a0 = $a0
	set var $bt_saved_wb = $windowbase
	set var $bt_saved_ws = $windowstart
	if $_verbose
		printf "  save_bt_state: }\n"
	end
end
document save_bt_state
  Internal Function to save stack context while walking thru task list.
end

define restore_bt_state
	if $_verbose
		printf "  restore_bt_state {\n"
	end
#	flush
	frame 0

	disable_bt

   	set $ptevaddr = $bt_saved_ptevaddr
   	set $pc = $bt_saved_pc
   	set $a0 = $bt_saved_a0
   	set $sp = $bt_saved_sp
        set $windowbase = $bt_saved_wb
        set $windowstart = $bt_saved_ws

	enable_bt

	set $ps = $bt_saved_ps

	if $_verbose
		printf "  restore_bt_state }\n"
	end
end
document restore_bt_state
  Internal Function to restore stack context while walking thru task list.
end
 

#
# This function starts with init_task (swapper), walks through task list, 
# and does conditional things like printing 'ps' or 'bt' info.
#
define for_each_task
#	printf "$argc: %d, $btiarg0: %d\n", $argc, $arg0
#	set var $pid = $arg0

	if $_verbose
		printf "for_each_task {\n"
	end
        set $offset = (char *) &init_task->tasks - (char *) &init_task
        set $initthread = &init_task
        set $tsk = &init_task
        set $prev_tsk =  (struct task_struct *) 0
        set $next_tsk =  (struct task_struct *) 0
	set $tsk_count = 0
	set $done = 0

	save_bt_state

        while ($done == 0)
		set var $ti = (struct thread_info *) $tsk->stack
		set var $thread = (struct thread_struct *) &$tsk.thread
		set var $cpu = $ti->cpu
		set var $flags = $ti->flags
		set var $status = $ti->status
		set var $running = (($flags & (1 << 17)) != 0)
		set $printed_bt = 0
		set $tsk_count++
		set $mm = $tsk->mm
		set $active_mm = $tsk->active_mm
		if $mm != 0
			set $total_vm = $mm->total_vm
			# set $cpu = $mm->context.cpu
			set $asid = $mm->context.asid[$cpu]
		else
			set $total_vm = -1
			set $asid = -1
			# set $cpu = -1
		end

		if $_verbose
			printf "$thread:%x, $ti:0x%x->{cpu:%d, flags:0x%x, status:0x%x}, $running:0x%x\n", $thread, $ti, $cpu, $flags, $status, $running
		end

                if (($_print_ps != 0) || ($_target_pid == $tsk->pid))
			if $mm
        			printf "tsk:%p->{pid:%5d, cpus_allowed:%x, state:%2d, running:%d, mm:0x%08x->{asid[%2d]:0x%08x, total_vm:%5d}, \tcomm:'%s'}\n", $tsk, $tsk->pid, (int)$tsk->cpus_allowed, $tsk->state, $running, $mm, $cpu, $asid, $total_vm, (char*)$tsk->comm
			else
        			printf "tsk:%p->{pid:%5d, cpus_allowed:%x, state:%2d, running:%d, mm:0x%08x,                                   \tcomm:'%s'}\n", $tsk, $tsk->pid, (int)$tsk->cpus_allowed, $tsk->state, $running, $mm, (char*)$tsk->comm
			end
		end

		if $_print_bt
			if ($_target_pid < 0 || ($_target_pid == $tsk->pid))

#				REMIND-FIXME: Need to Back-Trace Swapper also.
				printf "-------------------------------------------------------------------------------------------------------------------------------------------------\n"
				if (!$running)
						switch_state($tsk)
						#
						# Switch to call4 ABI for kernel backtraces
						#
						set $saved_ps = $ps
						set $psexcm = 0
						set $pswoe = 1
						bt
						if ($_target_pid == $tsk->pid) 
							save_bt_state
						end	
						set $ps = $saved_ps
					else
						if ($cpu == $prid)
							# 
							# This is the current task actually running
							# go back to it's original state but don't
							# we continuie to mark it as valid.
							#
							restore_state
							set var $_saved_state_valid = 1
							bt
						else
							printf "                         CURRENTLY RUNNING ON ANOTHER CPU  $cpu:%d != $prid:%d)\n", $cpu, $prid
						end
				end
				set $printed_bt = 1
				printf "-------------------------------------------------------------------------------------------------------------------------------------------------\n"

				if ($_target_pid == $tsk->pid) 
					set var $current = $tsk
					set var $done = 1
				end
			end
		end

		if ($tsk->state == 0) 
			set $current_running = $tsk
		end

		if ($done == 0)
			set $prev_tsk = $tsk
			set $next_tsk_ptr = $tsk->tasks.next
                	set $tsk = (struct task_struct *)((char*)$prev_tsk->tasks.next - $offset)
                	set $next_tsk = (struct task_struct *)((char*)$tsk->tasks.next - $offset)

			if $_verbose
                		printf "$tsk:%x = prev_tsk:%x->tasks.next:%x - $offset:%x)\n\n\n", $tsk, $prev_tsk, $next_tsk_ptr, $offset
			end
		end

		if ($next_tsk == $initthread)
			set var $done = 1
		end

		if $printed_bt
			printf "\n\n"
		end
        end

	restore_bt_state

	if $_verbose
		printf "for_each_task }\n"
	end
end
document for_each_task
  Internal Function to walk through task list and perform conditional actions (Ex: back-trace).
end




