/* Invokes a system call with the stack pointer (%esp) set to a
   bad address.  The process must be terminated with -1 exit
   code. 

   For Project 3: The bad address lies approximately 64MB below
   the code segment, so there is no ambiguity that this attempt
   must be rejected even after stack growth is implemented.
   Moreover, a good stack growth heuristics should probably not
   grow the stack for the purpose of reading the system call
   number and arguments. */

#include <stdio.h>
#include "../tests/lib.h"

int
main (void) 
{
  asm volatile ("movl $.-(64*1024*1024), %esp; int $0x30");
  printf ("should have called exit(-1)");
  return 0;
}
