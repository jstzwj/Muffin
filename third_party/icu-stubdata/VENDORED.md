# ICU empty data stub

Unmodified `source/stubdata/stubdata.cpp` and `stubdata.h` from ICU release-74-2:
https://github.com/unicode-org/icu/tree/release-74-2/icu4c/source/stubdata

License: Unicode-3.0, reproduced in LICENSE from the matching Conan package.

Muffin links the official empty stub into MuffinUi as a regular object. It
satisfies the static ICU data symbol before archive extraction, preventing the
complete data array from being embedded. `cmake/MuffinIcuData.cmake` fetches
the matching official `icu4c-74_2-data-bin-l.zip` (or big-endian `-b.zip`),
verifies its pinned SHA-256 and deploys the prebuilt common-format `.dat`.
`IcuRuntime.cpp` verifies and memory-maps the file, then registers it using
`udata_setCommonData`. No ICU_DATA environment variable or system ICU is used.
