#include <stdio.h>
#include "separate.c"
int main(void) { test_main(); printf("A %08x  B %08x  C %08x  D %08x  A=C %u  B=D %u\n", (unsigned)check_a, (unsigned)check_b, (unsigned)check_c, (unsigned)check_d, a_equals_c, b_equals_d); return 0; }
