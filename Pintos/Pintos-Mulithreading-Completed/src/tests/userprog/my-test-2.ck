# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(my-test-2) begin
(my-test-2) end
my-test-2: exit(0)
EOF
pass;
