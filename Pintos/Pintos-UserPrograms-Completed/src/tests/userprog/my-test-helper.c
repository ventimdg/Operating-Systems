/* helper*/

#include <stdio.h>
#include "tests/lib.h"

int main(void) {
  test_name = "my-test-helper";
  msg("This is the child, going into grandchild!");
  pid_t grandchild = wait(exec("child-simple"));

  return 20;
}
