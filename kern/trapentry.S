/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>
#include <kern/macro.h>



###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushq $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushq $0;							\
	pushq $(num);							\
	jmp _alltraps

.text

/*
 * Lab 3: Your code here for generating entry points for the different traps.
   Find the PUSHA,POPA macros in kern/macro.h. Since amd64 doesn't support
   pusha,popa so you will find these macros handy.
 */
	TRAPHANDLER_NOEC(TEST_DIVIDE,0)
	TRAPHANDLER_NOEC(TEST_DEBUG,1)
	TRAPHANDLER_NOEC(TEST_NMI,2)
	TRAPHANDLER_NOEC(TEST_BRKPT,3)
	TRAPHANDLER_NOEC(TEST_OFLOW,4)
	TRAPHANDLER_NOEC(TEST_BOUND,5)
	TRAPHANDLER_NOEC(TEST_ILLOP,6)
	TRAPHANDLER_NOEC(TEST_DEVICE,7)
	TRAPHANDLER(TEST_DBLFLT,8)
	TRAPHANDLER_NOEC(TEST_RESERVED,9)
	TRAPHANDLER(TEST_TSS,10)
	TRAPHANDLER(TEST_SEGNP,11)
	TRAPHANDLER(TEST_STACK,12)
	TRAPHANDLER(TEST_GPFLT,13)
	TRAPHANDLER(TEST_PGFLT,14)
	TRAPHANDLER_NOEC(TEST_RES,15)
	TRAPHANDLER_NOEC(TEST_FPERR,16)
	TRAPHANDLER_NOEC(TEST_ALIGN,17)
	TRAPHANDLER_NOEC(TEST_MCHK,18)
	TRAPHANDLER_NOEC(TEST_SIMDERR,19)

	TRAPHANDLER_NOEC(TEST_SYSCALL,48)

/*
 * Lab 3: Your code here for _alltraps
 *
 * Hint: Be sure to review the x64 calling convention from lab1
 *       for how to pass a pointer to the trapframe.
 */

_alltraps:	
		sub $8, %rsp
		movw %ds, (%rsp)
		sub $8, %rsp
		movw %es, (%rsp)

		PUSHA
	
		movq $GD_KD, %rax
		movq %rax, %ds
		movq %rax, %es
		movq %rsp, %rdi		
	
		call trap

		POPA_
		movw (%rsp), %es
		add $8, %rsp	
		movw (%rsp), %ds
		add $8, %rsp
		iret
