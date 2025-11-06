#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>

// Return a uniform random integer in [min_val, max_val] inclusive.
size_t uniform_random_range(size_t min_val, size_t max_val);

// Shuffle a vector of strings in-place using the internal PRNG.
void shuffle_strings(std::vector<std::string> &v);