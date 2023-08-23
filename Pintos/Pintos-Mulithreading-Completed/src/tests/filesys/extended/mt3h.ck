# -*- perl -*-
use strict;
use warnings;
use tests::tests;
use tests::random;
check_expected ([<<'EOF']);
(mt3h) begin
(mt3h) empty
(mt3h) validfiledes
(mt3h) writesdone
(mt3h) readsdone
(mt3h) end
mt3h: exit(0)
EOF
pass;
