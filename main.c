#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "%s: invalid number of arguments\n", argv[0]);
    return 1;
  }

  printf("  .globl main\n");
  printf("main:\n");
  printf("  li.d $a0, %d\n", atoi(argv[1]));
  printf("  jr $ra\n");

  printf(".LFE0:\n");
  printf("  .size   main, .-main\n");
  printf("  .section  .note.GNU-stack,\"\",@progbits\n");

  return 0;
}
