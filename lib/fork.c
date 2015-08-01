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

    if (!(err & 2 && uvpt[(uint32_t)addr/PGSIZE] & PTE_COW))
        panic("pgfault not writable page\n");

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.
    if ((r = sys_page_alloc(sys_getenvid(), PFTEMP, PTE_W | PTE_P| PTE_U)))
        panic("sys_page_alloc returned %e\n", r);
    memmove((char *)PFTEMP, (char *)ROUNDDOWN((uint32_t)utf->utf_fault_va, PGSIZE), PGSIZE);
     if ((r = sys_page_map(sys_getenvid(), (void *)PFTEMP, sys_getenvid(), (void *)ROUNDDOWN((uint32_t)utf->utf_fault_va, PGSIZE),
                     PTE_W | PTE_P| PTE_U)))
         panic("sys_page_map returned %e\n", r);
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
	int r, flags;
    unsigned int va;
    
    va = pn * PGSIZE;
    flags = (uvpt[pn] & PTE_W || uvpt[pn] & PTE_COW)?( PTE_P | PTE_COW | PTE_U):PGOFF(uvpt[pn]);
    if ((r = sys_page_map(sys_getenvid(), (void *)va, envid, (void *)va, flags)))
        return r;
    if ((r = sys_page_map(sys_getenvid(), (void *)va, sys_getenvid(), (void *)va, flags)))
        return r;

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
    int r;
    envid_t cpid;
    register unsigned int i, j;
    unsigned int pn;

    set_pgfault_handler(pgfault);
    if ((cpid = sys_exofork()) == -1) 
        panic("sys_exofork returned %e\n", cpid);
    else if (cpid) {
        //uvpd = page directory
        for (i = 0; i < PDX(UTOP); i++)
            //page_table exists
            if (uvpd[i] & PTE_P)
                for (j = 0; j < NPTENTRIES; j++) {
                    pn = i * NPTENTRIES + j;
                    //if page exists
                    if (uvpt[pn] & (PTE_P|PTE_U) &&
                            pn != PGNUM(UXSTACKTOP - PGSIZE))
                        if ((r = duppage(cpid, pn)))
                            panic("duppage %e\n", r);
                }
        if ((r = sys_env_set_pgfault_upcall(cpid, thisenv->env_pgfault_upcall)))
            panic("sys_env_set_pgfault_upcall returned %e\n", r);
        if ((r = sys_page_alloc(cpid, (void *)UXSTACKTOP-PGSIZE, PTE_U|PTE_P|PTE_W)))
            panic("sys_page_alloc returned %e\n", r);
        if ((r = sys_env_set_status(cpid, ENV_RUNNABLE)))
            panic("sys_env_set_status returned %e\n", r);
    }
    else
        thisenv = &envs[ENVX(sys_getenvid())];
    return cpid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
