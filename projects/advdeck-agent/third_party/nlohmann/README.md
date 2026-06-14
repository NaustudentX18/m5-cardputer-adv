# nlohmann/json (vendored)

Single-header C++ JSON library.

- **Upstream:** https://github.com/nlohmann/json
- **Vendored version:** v3.12.0
- **License:** MIT (https://github.com/nlohmann/json/blob/develop/LICENSE.MIT)
- **Vendored file:** `nlohmann/json.hpp` (downloaded from
  `https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp`)

The library is a pure header, has no transitive dependencies, and compiles on
host (g++/clang) and on the Arduino/ESP32 toolchain. It is intentionally
vendored rather than fetched at build time so the project builds offline
without network access.

Include with `#include <nlohmann/json.hpp>`; the build adds
`-I third_party/nlohmann` so the include resolves to
`third_party/nlohmann/nlohmann/json.hpp`.

If you need to update: download the new `json.hpp` from the matching GitHub
release and place it at `third_party/nlohmann/nlohmann/json.hpp`; do not
vendor the multi-file amalgam.
