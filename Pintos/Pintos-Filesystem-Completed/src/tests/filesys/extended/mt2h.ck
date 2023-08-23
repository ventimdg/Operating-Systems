# -*- perl -*-
use strict;
use warnings;
use tests::tests;
use tests::random;
check_expected ([<<'EOF']);
(mt2h) begin
(mt2h) empty
(mt2h) validfiledes
(mt2h) valid
(mt2h) end
mt2h: exit(0)
EOF
pass;
