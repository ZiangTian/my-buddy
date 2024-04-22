#include "kernel/types.h"
#include "user/user.h"


int main(int argc, char *argv[])
{
  int count = getprocs();
  printf("There are %d active processes.\n", count);

  printf("Entering memory allocation demontration...\n");

  demo();




  exit(0);
}
