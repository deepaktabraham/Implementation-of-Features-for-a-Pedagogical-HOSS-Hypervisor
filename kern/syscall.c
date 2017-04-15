/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/time.h>
#ifndef VMM_GUEST
#include <vmm/ept.h>
#include <vmm/vmx.h>
#endif

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.

	// LAB 3: Your code here.
	user_mem_assert(curenv, s, len, PTE_U);
	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
static int
sys_cgetc(void)
{
	return cons_getc();
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	if (e == curenv)
		cprintf("[%08x] exiting gracefully\n", curenv->env_id);
	else
		cprintf("[%08x] destroying %08x\n", curenv->env_id, e->env_id);
	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
static envid_t
sys_exofork(void)
{
	// Create the new environment with env_alloc(), from kern/env.c.
	// It should be left as env_alloc created it, except that
	// status is set to ENV_NOT_RUNNABLE, and the register set is copied
	// from the current environment -- but tweaked so sys_exofork
	// will appear to return 0.

	// LAB 4: Your code here.
	struct Env* child;
	int r = env_alloc(&child, curenv->env_id);
	if(r < 0) {
		cprintf("\nenv_alloc, sys_exofork %e \n",r);
		return r;
	}
	child->env_status = ENV_NOT_RUNNABLE;
	child->env_tf = curenv->env_tf;
	child->env_tf.tf_regs.reg_rax = 0; //setting return value for child
	child->env_parent_id = curenv->env_id;
	return child->env_id;
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	// Hint: Use the 'envid2env' function from kern/env.c to translate an
	// envid to a struct Env.
	// You should set envid2env's third argument to 1, which will
	// check whether the current environment has permission to set
	// envid's status.

	// LAB 4: Your code here.
	if((status != ENV_RUNNABLE) && (status != ENV_NOT_RUNNABLE)) {
		cprintf("\n improper status %e\n", -E_INVAL);
		return -E_INVAL;
	}
	struct Env* envnow;
	int r = envid2env(envid, &envnow, 1);
	if(r < 0) {
		cprintf("\n envid2env %e\n", r);
		return r;
	}
	envnow->env_status = status;
	return 0;
}

// Set envid's trap frame to 'tf'.
// tf is modified to make sure that user environments always run at code
// protection level 3 (CPL 3) with interrupts enabled.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
	// LAB 5: Your code here.
	// Remember to check whether the user has supplied us with a good
	// address!
    struct Env *envnow;
    int r = envid2env(envid, &envnow, 1); 
    if (r<0) {
        cprintf("\n bad ENvid sys_env_set_pgfault_upcall %e \n",r);
        return r;
    }
    envnow->env_tf = *tf;
	envnow->env_tf.tf_cs |= 3;
   	envnow->env_tf.tf_eflags |= FL_IF;

    return 0;
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	// LAB 4: Your code here.
	struct Env *newenv;
	int r = envid2env(envid, &newenv, 1);
	if (r<0) {
		cprintf("\n bad ENvid sys_env_set_pgfault_upcall %e \n",r);
		return r;
	}
	newenv->env_pgfault_upcall = func;
	return 0;
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	// Hint: This function is a wrapper around page_alloc() and
	//   page_insert() from kern/pmap.c.
	//   Most of the new code you write should be to check the
	//   parameters for correctness.
	//   If page_insert() fails, remember to free the page you
	//   allocated!

	// LAB 4: Your code here.
	struct Env* envnow;
	int r = envid2env(envid, &envnow, 1);
	if(r<0){
		cprintf("\nenvid2end sys_page_alloc %e \n", r);
		return r;
	}
	struct PageInfo *pp = page_alloc(0);
	if (!pp) {
		cprintf("\n No memory to allocate page SYS_PAGE_ALLOC %e \n", -E_NO_MEM);
		return -E_NO_MEM;
	}
	if ((uint64_t)va >= UTOP || PGOFF(va))
    	return -E_INVAL;

	int newperm = PTE_U | PTE_P;
	if ((perm & newperm) != newperm || (perm & ~PTE_SYSCALL)) {
		cprintf("\n permission error %e sys_page_alloc\n", -E_INVAL);
		return -E_INVAL;
	}
	if (page_insert(envnow->env_pml4e, pp, va, perm) < 0) {
		cprintf("\n No memory to allocate page SYS_PAGE_ALLOC %e \n", -E_NO_MEM);
		page_free(pp);
		return -E_NO_MEM;
	}
	//memset(page2kva(pp), 0, PGSIZE);	
	return 0;

}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
static int
sys_page_map(envid_t srcenvid, void *srcva,
	     envid_t dstenvid, void *dstva, int perm)
{
	// Hint: This function is a wrapper around page_lookup() and
	//   page_insert() from kern/pmap.c.
	//   Again, most of the new code you write should be to check the
	//   parameters for correctness.
	//   Use the third argument to page_lookup() to
	//   check the current permissions on the page.

	// LAB 4: Your code here.
	struct Env *srcenv, *dstenv;
	int r1 = envid2env(srcenvid, &srcenv, 1);
	int r2 = envid2env(dstenvid, &dstenv, 1);
	/* Proper envid check*/
	if (r1 < 0 || r2 < 0) {
		cprintf("\n envid2env error %e, %e sys_page_map\n", r1, r2);
		return -E_BAD_ENV;
	}
	/*Address range check*/
	if((uintptr_t)srcva >= UTOP || (uintptr_t)dstva >= UTOP || PGOFF(srcva) || PGOFF(dstva)) {
		cprintf("\n envid2env error %e sys_page_map\n", -E_INVAL);
		return -E_INVAL;
	}
	/*Correct page request check*/
	struct PageInfo *map;
	pte_t *p_entry;
	map = page_lookup(srcenv->env_pml4e, srcva, &p_entry);
	if(!map) {
		cprintf("\n No page available or not mapped properly SYS_PAGE_ALLOC %e \n", -E_NO_MEM);
		return -E_NO_MEM;
	}
	/*Proper Permission check*/
	int map_perm = PTE_P | PTE_U;
	if ((perm & map_perm) != map_perm || (perm & ~PTE_SYSCALL)) {
		cprintf("\n permission error %e sys_page_map\n", -E_INVAL);
		return -E_INVAL;
	}
	if((perm & PTE_W) && !(*p_entry & PTE_W)) {
		cprintf("\n permission error %e sys_page_map\n", -E_INVAL);
		return -E_INVAL;
	}
	/*Page insert check*/
	if(page_insert(dstenv->env_pml4e, map, dstva, perm) < 0) {
		cprintf("\n No memory to allocate page SYS_PAGE_MAP %e \n", -E_NO_MEM);
		return -E_NO_MEM;
	}
	return 0;
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	// Hint: This function is a wrapper around page_remove().

	// LAB 4: Your code here.
	struct Env* envnow;
	int r = envid2env(envid, &envnow, 1);
	if(r < 0) {
		cprintf("\n envid2env error %e sys_page_map\n", r);
		return r;
	}
	if ((uint64_t)va >= UTOP || PGOFF(va))
    	return -E_INVAL;
	page_remove(envnow->env_pml4e, va);
	return 0;
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.  (Hint: does the
// sys_ipc_recv function ever actually return?)
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	// LAB 4: Your code here.
	struct Env *recvr;
	int r = envid2env(envid, &recvr, 0);	
	if (r < 0) {
		cprintf("\n Bad ENV\n");
		return r;
	}
	if (recvr->env_ipc_recving == 0) {
		return -E_IPC_NOT_RECV;
	}
	recvr->env_ipc_recving = 0;
	recvr->env_ipc_from = curenv->env_id;
	recvr->env_ipc_perm = 0;
	if((srcva && (srcva < (void*)UTOP)) && ((recvr->env_ipc_dstva) && (recvr->env_ipc_dstva < (void*)UTOP))){
		// cprintf("\n Sending page to the receiver \n"); [OBSOLETE]
		if(PGOFF(srcva)) {
			cprintf("\n Not pageAligned\n");
			return -E_INVAL;
		}
		int map_perm = PTE_U | PTE_P;
		if(((perm & map_perm) != map_perm) || (perm & ~PTE_SYSCALL)) {
			cprintf("\nPermission error\n");
			return -E_INVAL;
		}
		pte_t* entry;
		struct PageInfo *map = page_lookup(curenv->env_pml4e, srcva, &entry);
		if(!(map) || ((perm & PTE_W) && !(*entry & PTE_W))) {
			cprintf("\n VA is not mapped in senders address space or Sending read only pages with write permissions not permissible\n");
			return -E_INVAL;
		}
		if(page_insert(recvr->env_pml4e, map, recvr->env_ipc_dstva , perm) < 0) {
			cprintf("\n No memory to map the page to target env\n");
			return -E_NO_MEM;
		}
		recvr->env_ipc_perm = perm;
	}
	recvr->env_ipc_value = value;
	recvr->env_status = ENV_RUNNABLE;
	return 0;
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva)
{
	// LAB 4: Your code here.
	curenv->env_ipc_recving = 1;
	if(dstva < (void*)UTOP) {
		if(PGOFF(dstva))
			return -E_INVAL;
	}	
	curenv->env_ipc_dstva = dstva;
	curenv->env_status = ENV_NOT_RUNNABLE;
	curenv->env_tf.tf_regs.reg_rax = 0;
	sched_yield();
	return 0;
}

// Return the current time.
static int
sys_time_msec(void)
{
	// LAB 6: Your code here.
	panic("sys_time_msec not implemented");
}


// Maps a page from the evnironment corresponding to envid into the guest vm 
// environments phys addr space. 
//
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or guest doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or guest_pa >= guest physical size or guest_pa is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate 
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables. 
//
// Hint: The TA solution uses ept_map_hva2gpa().  A guest environment uses 
//       env_pml4e to store the root of the extended page tables.
// 
#ifndef VMM_GUEST
static void
sys_vmx_list_vms() {
	vmx_list_vms();
}

static bool
sys_vmx_sel_resume(int i) {
	return vmx_sel_resume(i);
}

static int
sys_vmx_get_vmdisk_number() {
	return vmx_get_vmdisk_number();
}

static void
sys_vmx_incr_vmdisk_number() {
	vmx_incr_vmdisk_number();
}


// Maps a page from the environment corresponding to envid into the guest vm 
// environments phys addr space. 
//
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or guest doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or guest_pa >= guest physical size or guest_pa is not page-aligned.
//	-E_INVAL is guest_pa is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate 
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables. 
//
// Hint: The TA solution uses ept_map_hva2gpa().  A guest environment uses 
//       env_pml4e to store the root of the extended page tables.
// 

static int
sys_ept_map(envid_t srcenvid, void *srcva,
	    envid_t guest, void* guest_pa, int perm)
{
	/* Your code here */
	
	struct Env* srcenv = NULL;
	struct Env* guestenv = NULL;
	int retVal = -1;
	pte_t* pte = NULL;
	pte_t* hva = NULL;
	struct PageInfo *page = NULL;
	
	retVal = envid2env(srcenvid, &srcenv, 1);
	if (retVal != 0)
	{
		cprintf("VC error: bad srcenvid\n");	
		return -E_BAD_ENV;
	}

	retVal = envid2env(guest, &guestenv, 1);
	if (retVal != 0)
	{
		cprintf("VC error: bad guest\n");	
		return -E_BAD_ENV;
	}
	
	if ((uint64_t)srcva >= UTOP)
	{
		cprintf("VC error: srcva greater than max user useable va\n");
		return -E_INVAL;
	}

	if (PGOFF(srcva))
	{
		cprintf("VC error: srcva is not page aligned\n");
		return -E_INVAL;
	}

	if ((uint64_t)guest_pa >= guestenv->env_vmxinfo.phys_sz)
	{
		cprintf("VC error: guest_pa greater than max guest physical size\n");
		return -E_INVAL;
	}

	if (PGOFF(guest_pa))
	{
		cprintf("VC error: guest_pa is not page aligned\n");
		return -E_INVAL;
    	}	
		
	page = page_lookup(srcenv->env_pml4e, srcva, &pte);
	if (!page) 
	{
		cprintf("VC error: page not found\n");
		return -E_INVAL;
	}

	if ((!perm) || ((perm & __EPTE_WRITE) && (!(*pte & PTE_W))))
	{
		cprintf("VC error: wrong permission\n");
		return -E_INVAL;
	}
	
	hva = page2kva(page);
	retVal = ept_map_hva2gpa(guestenv->env_pml4e, hva, guest_pa, perm, 0);
	if (retVal != 0) 
	{
		cprintf("VC error: hva to gpa failed\n");
		return retVal;
	}
	
	page->pp_ref++;
	return 0;
}

static envid_t
	sys_env_mkguest(uint64_t gphysz, uint64_t gRIP) {
	int r;
	struct Env *e;

	// Check if the processor has VMX support.
	if ( !vmx_check_support() ) {
		return -E_NO_VMX;
	} else if ( !vmx_check_ept() ) {
		return -E_NO_EPT;
	} 
	if ((r = env_guest_alloc(&e, curenv->env_id)) < 0)
		return r;
	e->env_status = ENV_NOT_RUNNABLE;
	e->env_vmxinfo.phys_sz = gphysz;
	e->env_tf.tf_rip = gRIP;
	return e->env_id;
}
#endif //!VMM_GUEST


// Dispatches to the correct kernel function, passing the arguments.
int64_t
syscall(uint64_t syscallno, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	// LAB 3: Your code here.

	switch (syscallno) {
	case SYS_cputs :
		sys_cputs((const char *)a1, (size_t)a2);
		return 0;
	case SYS_cgetc :
		return sys_cgetc();
	case SYS_getenvid :
		return sys_getenvid();
	case SYS_env_destroy :
		return sys_env_destroy(a1);
	case SYS_yield:
		sys_yield();
	case SYS_exofork:
		return sys_exofork();
	case SYS_env_set_status:
		return sys_env_set_status((envid_t)a1, (int)a2);
	case SYS_page_alloc:
		return sys_page_alloc((envid_t)a1, (void*)a2, (int)a3);
	case SYS_page_map:
		return sys_page_map((envid_t)a1, (void *)a2,(envid_t) a3, (void *)a4, (int) a5);
	case SYS_page_unmap:
		return sys_page_unmap((envid_t)a1, (void*)a2);
	case SYS_env_set_pgfault_upcall:
		return sys_env_set_pgfault_upcall((envid_t)a1, (void*)a2);
	case SYS_ipc_try_send:
		return sys_ipc_try_send((envid_t)a1, (uint32_t)a2, (void*)a3, (unsigned)a4);
	case SYS_ipc_recv:
		return sys_ipc_recv((void*)a1);
	case SYS_env_set_trapframe:
		return sys_env_set_trapframe((envid_t)a1, (struct Trapframe *)a2);
#ifndef VMM_GUEST
	case SYS_ept_map:
		return sys_ept_map(a1, (void*) a2, a3, (void*) a4, a5);
	case SYS_env_mkguest:
		return sys_env_mkguest(a1, a2);
	case SYS_vmx_list_vms:
		sys_vmx_list_vms();
		return 0;
	case SYS_vmx_sel_resume:
		return sys_vmx_sel_resume(a1);
	case SYS_vmx_get_vmdisk_number:
		return sys_vmx_get_vmdisk_number();
	case SYS_vmx_incr_vmdisk_number:
		sys_vmx_incr_vmdisk_number();
		return 0;
#endif
		
	default:
		return -E_NO_SYS;
	}
}

#ifdef TEST_EPT_MAP
int
_export_sys_ept_map(envid_t srcenvid, void *srcva,
		    envid_t guest, void* guest_pa, int perm)
{
	return sys_ept_map(srcenvid, srcva, guest, guest_pa, perm);
}
#endif

