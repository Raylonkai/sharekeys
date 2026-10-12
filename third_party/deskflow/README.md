# Deskflow attribution

`mouse_buttons.h` adapts `MSWindowsDesks::fakeMouseButton` from
[src/lib/platform/MSWindowsDesks.cpp](https://github.com/deskflow/deskflow/blob/3aec96d57eda8851e3ca534400962cd9d71357ac/src/lib/platform/MSWindowsDesks.cpp),
commit `3aec96d57eda8851e3ca534400962cd9d71357ac`.

The adapted encoder preserves button mapping and the destination's swapped-button
compensation, replaces Deskflow types with ShareKeys types, and writes a
`MOUSEINPUT` structure instead of posting a Deskflow message. It is compiled into
the Windows backend. No Deskflow networking or executable is bundled.

The original copyright and SPDX notice are retained in the source. License texts
are included here: GPL-2.0-only with the upstream OpenSSL exception. Distribution
of a combined application must account for these license terms. No license for
other preexisting ShareKeys code is asserted by this notice.
