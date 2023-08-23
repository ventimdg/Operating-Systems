#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"'

void test_main(void) {
  int getwrites = block_writes(0);
  int getreads = block_writes(1);

  const char* name = "empty";
  int maximum = 102400;
  CHECK(create(name, 0), "empty");
  int file_des = open(name);
  CHECK(file_des > 0, "validfiledes");
  int read_counter = 0;
  int write_counter = 0;
  int counter = 0;

  write(file_des, &write_counter, 102400);

  int getwrites2 = block_writes(0);
  int getreads2 = block_writes(1);

  CHECK(getwrites2 - getwrites > 100 && getwrites2 - getwrites < 300, "writesdone");

  CHECK(getreads2 - getreads >= 0 && getreads2 - getreads < 15, "readsdone");

  close(file_des);
}