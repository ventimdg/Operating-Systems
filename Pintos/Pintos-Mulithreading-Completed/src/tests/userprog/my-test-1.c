/* grandparent to grandchild, waiting and checking the order of exits*/

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  pid_t child = wait(exec("my-test-helper"));
  msg("Back at Parent");
}
