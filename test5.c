#include "sut.h"
#include <stdio.h>
#include <string.h>

void hello3() {
  int i, fd;
  int buf_size = 2048;
  char read_sbuf[buf_size];
  printf("striving to open test5\n");
  fd = sut_open("./test5.txt");

  printf("opened file %d\n", fd);
  sut_yield();
  if (fd < 0)
    printf("Error: sut_open() failed in hello3()\n");
  else {
    char *read_result = sut_read(fd, read_sbuf, buf_size);
    if (read_result != NULL) {
      printf("%s", read_sbuf);
      sut_close(fd);
    } else {
      printf("Error: sut_read() failed\n");
    }
  }
  sut_exit();
}

void hello1() {
  int i, fd;
  char write_sbuf[128];
  printf("running open\n");
  fd = sut_open("./test5.txt");
  printf("opened file %d\n", fd);
  if (fd < 0)
    printf("Error: sut_open() failed\n");
  else {
    printf("helllooss\n");
    for (i = 0; i < 5; i++) {
      sprintf(write_sbuf, "Hello world!, message from SUT-One i = %d \n", i);
      sut_write(fd, write_sbuf, strlen(write_sbuf));
      sut_yield();
    }
    sut_close(fd);
    printf("closed %d\n", fd);
    //sut_create(hello3);
  }

  sut_exit();
  printf("came back to returned?\n");
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
  sut_init();
  sut_create(hello1);
  sut_create(hello2);
  sut_shutdown();
}
