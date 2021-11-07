#include "sut.h"
#include <stdio.h>

void hello1() {
  int i;
  for (i = 0; i < 100; i++) {
    printf("Hello world!, this is SUT-One \n");
    sut_yield();
  }
  sut_exit();
}

void hello2() {
  int i;
  for (i = 0; i < 100; i++) {
    printf("Hello world!, this is SUT-Two \n");
    sut_yield();
  }
  sut_exit();
}

int main() {
  bool was_init = sut_init();
  printf("Successfully initiated sut (%d)\n", was_init);
  bool t1 = sut_create(hello1);
  if (t1 == false)
    printf("Error: sut_create(hello1) failed\n");
  bool t2 = sut_create(hello2);
  if (t2 == false)
    printf("Error: sut_create(hello2) failed\n");
  sut_shutdown();
}
