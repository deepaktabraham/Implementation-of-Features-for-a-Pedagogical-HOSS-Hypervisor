// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).
	// LAB 4: Your code here.
	pte_t entry = uvpt[VPN(addr)];
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.
	if((err & FEC_WR) && (uvpt[VPN(addr)] & PTE_COW)) {
		if(sys_page_alloc(0, (void*)PFTEMP, PTE_U|PTE_P|PTE_W) == 0) {
			void *pg_addr = ROUNDDOWN(addr, PGSIZE);
			memmove(PFTEMP, pg_addr, PGSIZE);
			r = sys_page_map(0, (void*)PFTEMP, 0, pg_addr, PTE_U|PTE_W|PTE_P);
			if (r < 0) {
				panic("pgfault...something wrong with page_map");
			}
			r = sys_page_unmap(0, PFTEMP);
			if (r < 0) {
				panic("pgfault...something wrong with page_unmap");
			}
			return;
		}
		else {
			panic("pgfault...something wrong with page_alloc");
		}
	}
	else {
			panic("pgfault...wrong error %e", err);	
	}
	// LAB 4: Your code here.
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
	pte_t entry = uvpt[pn];
	void* addr = (void*) ((uintptr_t)pn * PGSIZE);
	int perm = entry & PTE_SYSCALL;
	if(perm& PTE_SHARE) {
		r = sys_page_map(0, addr, envid, addr, perm);
		if(r < 0) {
			panic("Something went wrong on duppage %e",r);
		}
	}
	else if((perm & PTE_COW) || (perm & PTE_W)) {
		perm &= ~PTE_W;
		perm |= PTE_COW;
		r = sys_page_map(0, addr, envid, addr, perm);
		if(r < 0) {
			panic("Something went wrong on duppage %e",r);
		}
		r = sys_page_map(0, addr, 0, addr, perm);
		if(r < 0) {
			panic("Something went wrong on duppage %e",r);
		}
	}
	else {
		r = sys_page_map(0, addr, envid, addr, perm);
		if(r < 0) {
			panic("Something went wrong on duppage %e",r);
		}
	}
	// LAB 4: Your code here.
	//panic("duppage not implemented");
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	int r=0;
	set_pgfault_handler(pgfault);
	envid_t childid = sys_exofork();
	if(childid < 0) {
		panic("\n couldn't call fork %e\n",childid);
	}
	if(childid == 0) {
		thisenv = &envs[ENVX(sys_getenvid())];	// some how figured how to get this thing...
		return 0; //this is for the child
	}
	r = sys_page_alloc(childid, (void*)(UXSTACKTOP-PGSIZE), PTE_P|PTE_W|PTE_U);
	if (r < 0)
		panic("\n couldn't call fork %e\n", r);
    
	uint64_t pml;
	uint64_t pdpe;
	uint64_t pde;
	uint64_t pte;
	uint64_t each_pde = 0;
	uint64_t each_pte = 0;
	uint64_t each_pdpe = 0;
	for(pml = 0; pml < VPML4E(UTOP); pml++) {
		if(uvpml4e[pml] & PTE_P) {
			
			for(pdpe = 0; pdpe < NPDPENTRIES; pdpe++, each_pdpe++) {
				if(uvpde[each_pdpe] & PTE_P) {
					
					for(pde= 0; pde < NPDENTRIES; pde++, each_pde++) {
						if(uvpd[each_pde] & PTE_P) {
							
							for(pte = 0; pte < NPTENTRIES; pte++, each_pte++) {
								if(uvpt[each_pte] & PTE_P) {
									
									if(each_pte != VPN(UXSTACKTOP-PGSIZE)) {
										r = duppage(childid, (unsigned)each_pte);
										if (r < 0)
											panic("\n couldn't call fork %e\n", r);

									}
								}
							}

						}
						else {
							each_pte = (each_pde+1)*NPTENTRIES;		
						}

					}

				}
				else {
					each_pde = (each_pdpe+1)* NPDENTRIES;
				}

			}

		}
		else {
			each_pdpe = (pml+1) *NPDPENTRIES;
		}
	}

	extern void _pgfault_upcall(void);	
	r = sys_env_set_pgfault_upcall(childid, _pgfault_upcall);
	if (r < 0)
		panic("\n couldn't call fork %e\n", r);

	r = sys_env_set_status(childid, ENV_RUNNABLE);
	if (r < 0)
		panic("\n couldn't call fork %e\n", r);
	
	// LAB 4: Your code here.
	//panic("fork not implemented");
	return childid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
