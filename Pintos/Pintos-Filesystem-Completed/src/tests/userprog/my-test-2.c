/* This test will test the functionality of the tell and seek system call! */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int fd_of_my_file = open("sample.txt");
  if (fd_of_my_file < 2) {
    fail("open() returned %d", fd_of_my_file);
  }
  unsigned x = 4;
  seek(fd_of_my_file, x);
  if (tell(fd_of_my_file) != x) {
    fail("tell/seek failed!");
  }
}
