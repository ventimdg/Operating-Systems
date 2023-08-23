#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  const char* name = "empty";
  CHECK(create(name, 0), "empty");
  int file_des = open(name);
  CHECK(file_des > 0, "validfiledes");
  int read_counter = 0;
  int write_counter = 0;
  int counter = 0;

  // write in
  int maximum = 1024 * 128;

  while (counter < maximum) {
    write(file_des, &write_counter, 4);
    counter += 4;
  }

  counter = 0;

  //set offset back to 0
  seek(file_des, 0);

  //read byte by byte

  while (counter < maximum) {
    char readcounter1;
    read(file_des, &readcounter1, 4);
    counter += 4;
  }

  int getwrites = block_writes(0);

  CHECK(getwrites < 1024, "valid");
  close(file_des);
}