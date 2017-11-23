# Implementation-of-Features-for-a-Pedagogical-HOSS-Hypervisor

The paravirtualized HOSS hypervisor is developed based on the MIT-JOS exokernel Operating System.
The HOSS hypervisor is developed by leveraging the Intel VT-x hardware extensions for virtualization.

This implementation is adapted for the Intel x86_64 architecture. Testing can be performed on a Bochs x86 emulator with a single x86_64 processor having VT-x extensions enabled and 256 MB of main memory.

### The following features have been implemented -
1 - Check for VMX support<br />
2 - Check for Extended Page Table support<br />
3 - Modifcations for scheduling a guest environment<br />
4 -	Implementation of Extended Page Table for handling Guest to Host physical address mappings<br />
5 - Assembly code to launch the Guest environment, by mapping JOS guest kernel and bootloader into the Virtual Machine’s memory segment<br />

### Steps
To run the JOS kernel: **`make bochs`**<br />
To launch the Guest VM after the JOS kernel boots up: **`vmm`**<br />

Implementation is based on the CSE591-Virtualization course at Stony Brook University.<br />
< https://www3.cs.stonybrook.edu/~porter/courses/cse591/s14/lab1.html >
