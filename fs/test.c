
#include <inc/x86.h>
#include <inc/string.h>

#include "fs.h"

static char *msg = "This is the NEW message of the day!\n\n";

void
fs_test(void)
{
	struct File *f;
	int r;
	char *blk;
	uint32_t *bits;


	if ((r = file_open("/not-found", &f)) < 0 && r != -E_NOT_FOUND)
		panic("file_open /not-found: %e", r);
	else if (r == 0)
		panic("file_open /not-found succeeded!");
	if ((r = file_open("/newmotd", &f)) < 0)
		panic("file_open /newmotd: %e", r);
	cprintf("file_open is good\n");

	if ((r = file_get_block(f, 0, &blk)) < 0)
		panic("file_get_block: %e", r);
	if (strcmp(blk, msg) != 0)
		panic("file_get_block returned wrong data");
	cprintf("file_get_block is good\n");

}
