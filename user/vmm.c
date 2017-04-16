#include <inc/lib.h>
#include <inc/vmx.h>
#include <inc/elf.h>
#include <inc/ept.h>
#include <inc/stdio.h>

#define GUEST_KERN "/vmm/kernel"
#define GUEST_BOOT "/vmm/boot"

#define JOS_ENTRY 0x7000

// Map a region of file fd into the guest at guest physical address gpa.
// The file region to map should start at fileoffset and be length filesz.
// The region to map in the guest should be memsz.  The region can span multiple pages.
//
// Return 0 on success, <0 on failure.
//
static int
map_in_guest( envid_t guest, uintptr_t gpa, size_t memsz, 
	      int fd, size_t filesz, off_t fileoffset ) {
	/* Your code here */
	int i, r;

	if (PGOFF(gpa) != 0)
	{
		ROUNDDOWN(gpa, PGSIZE);
	}

	//cprintf("File Size = %d, Mem Size = %d\n",filesz,memsz);
    for (i = 0; i < memsz; i += PGSIZE) 
    {
    	if (i < filesz)
    	{
			//Allocate a blank page in FS environment to receive data
		    r = sys_page_alloc(0, UTEMP, PTE_P | PTE_U | PTE_W);
			if (r < 0)
				return r;

			//Seek from fileoffset position
		    r = seek(fd, fileoffset + i);
			if (r < 0)
				return r;

			//Read file contents page-wise into the blank page
		    r = readn(fd, UTEMP, MIN(PGSIZE, filesz-i));
			if (r<0) 
				return r;

			//Map page into the guest
		    r = sys_ept_map(thisenv->env_id, (void*)UTEMP, guest, (void*) (gpa + i), __EPTE_FULL);
			if (r < 0)
				panic("Something wrong with map_in_guest after calling sys_ept_map: %e", r);

			//Unmap page in FS environment since not needed
		    sys_page_unmap(0, UTEMP);	   
    	}

    	else
    	{
    		//cprintf("File Size = %d, Mem size = %d\n",filesz, memsz);
			r = sys_page_alloc(thisenv->env_id, (void*) UTEMP, __EPTE_FULL);
			if (r < 0)
				return r;

	   		r = sys_ept_map(thisenv->env_id, UTEMP, guest, (void *)(gpa + i), __EPTE_FULL);
			
			if (r < 0)
			panic("Something wrong with sys_ept_map: %e", r);
	    	
	    	sys_page_unmap(thisenv->env_id, UTEMP);
    	}
    	
	}
	return 0;
} 

// Read the ELF headers of kernel file specified by fname,
// mapping all valid segments into guest physical memory as appropriate.
//
// Return 0 on success, <0 on error
//
// Hint: compare with ELF parsing in env.c, and use map_in_guest for each segment.
static int
copy_guest_kern_gpa( envid_t guest, char* fname ) {

	/* Your code here */
	int fd,ret;
	char data_buffer[512];
	struct Elf* elfheader;

	fd = open(fname,O_RDONLY);	
	if (fd < 0)
	{
		cprintf("File does not exist\n");
		return -E_NOT_FOUND;
	}
	
	if(readn(fd,data_buffer,sizeof(data_buffer)) != sizeof(data_buffer))
	{
		close(fd);
		cprintf("VC: There is something wrong in reading the file fname\n");
		return -E_NO_SYS;
	}

	elfheader = (struct Elf*) data_buffer;

	if(elfheader->e_magic != ELF_MAGIC)
	{
		close(fd);
		cprintf("VC: Error in magic number of given elf file\n");
		return -E_NO_SYS;
	}

	struct Proghdr* progheader = (struct Proghdr*)(data_buffer + elfheader->e_phoff);
	struct Proghdr* endph = progheader + elfheader->e_phnum;

	for (;progheader<endph;progheader++)
	{
		if (progheader->p_type == ELF_PROG_LOAD)
		{
			ret = map_in_guest(guest,progheader->p_pa,progheader->p_memsz,fd,progheader->p_filesz,progheader->p_offset);
			if (ret<0)
			{
				close(fd);
				cprintf("VC: Map in Guest Unsuccessful.\n");
				return -E_NO_SYS;
			}
		}	
	}
	close(fd);
	cprintf("VC: Copy kern to gpa success!!\n");
	return ret;
}

void
umain(int argc, char **argv) {
	int ret;
	envid_t guest;
	char filename_buffer[50];	//buffer to save the path 
	int vmdisk_number;
	int r;
	if ((ret = sys_env_mkguest( GUEST_MEM_SZ, JOS_ENTRY )) < 0) {
		cprintf("Error creating a guest OS env: %e\n", ret );
		exit();
	}
	guest = ret;

	// Copy the guest kernel code into guest phys mem.
	if((ret = copy_guest_kern_gpa(guest, GUEST_KERN)) < 0) {
		cprintf("Error copying page into the guest - %d\n.", ret);
		exit();
	}

	// Now copy the bootloader.
	int fd;
	if ((fd = open( GUEST_BOOT, O_RDONLY)) < 0 ) {
		cprintf("open %s for read: %e\n", GUEST_BOOT, fd );
		exit();
	}

	// sizeof(bootloader) < 512.
	if ((ret = map_in_guest(guest, JOS_ENTRY, 512, fd, 512, 0)) < 0) {
		cprintf("Error mapping bootloader into the guest - %d\n.", ret);
		exit();
	}
#ifndef VMM_GUEST	
	sys_vmx_incr_vmdisk_number();	//increase the vmdisk number
	//create a new guest disk image
	
	vmdisk_number = sys_vmx_get_vmdisk_number();
	snprintf(filename_buffer, 50, "/vmm/fs%d.img", vmdisk_number);
	
	cprintf("Creating a new virtual HDD at /vmm/fs%d.img\n", vmdisk_number);
        r = copy("vmm/clean-fs.img", filename_buffer);
        
        if (r < 0) {
        	cprintf("Create new virtual HDD failed: %e\n", r);
        	exit();
        }
        
        cprintf("Create VHD finished\n");
#endif
	// Mark the guest as runnable.
	sys_env_set_status(guest, ENV_RUNNABLE);
	wait(guest);
}


