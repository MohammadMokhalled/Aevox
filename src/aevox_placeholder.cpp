// src/aevox_placeholder.cpp
//
// Placeholder translation unit.
// Purpose: prevents empty-archive linker warnings on macOS (ar) and some
//          Linux archivers when aevox_core has no real source files yet.
//
// Removal: delete this file and remove it from src/CMakeLists.txt when
//          AEV-001 adds the first real source file (src/net/asio_executor.cpp).
//          The AEV-001 Developer Log tracks this removal.

namespace aevox {
// intentionally empty
} // namespace aevox
