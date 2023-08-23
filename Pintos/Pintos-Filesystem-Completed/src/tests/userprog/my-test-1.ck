# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(my-test-1) begin
(my-test-helper) This is the child, going into grandchild!
(child-simple) run
child-simple: exit(81)
my-test-helper: exit(20)
(my-test-1) Back at Parent
(my-test-1) end
my-test-1: exit(0)
EOF
pass;