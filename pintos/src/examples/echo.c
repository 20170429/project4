#include <stdio.h>
#include <syscall.h>

int
main (int argc, char **argv)
{
  wait (exec ("child-simple"));
  wait (exec ("child-simple"));
  wait (exec ("child-simple"));
  wait (exec ("child-simple"));

  return EXIT_SUCCESS;
}
