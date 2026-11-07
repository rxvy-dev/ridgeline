#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Decodes a zlib stream. `expected` (if non-zero) is the exact output size.
bool zlibInflate(const uint8_t* src, size_t n, std::vector<uint8_t>& out, size_t expected = 0);
