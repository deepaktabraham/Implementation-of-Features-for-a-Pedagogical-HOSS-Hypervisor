//
// Simple inline assembly example
// 
// For JOS lab 1 exercise 1
//

#include <stdio.h>

int
main(int argc, char **argv)
{
  int x = 1;
  printf("Hello x = %d\n", x);
  
  // Your code goes here?
  asm( "leal (%1,%1,1), %0" : "=a" (x) : "b" (x)); 

  printf("Hello x = %d after increment\n", x);

  if(x == 2){
    printf("OK\n");
  }
  else{
    printf("ERROR\n");
  }
}
