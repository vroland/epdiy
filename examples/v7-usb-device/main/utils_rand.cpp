#include "utils_rand.h"

#include <atomic>
#include <cstdint>
#include "esp_system.h"
#include "esp_timer.h"         // esp_timer_get_time()
#include "esp_heap_caps.h"     // esp_get_free_heap_size()
#include <stdint.h>
#include <cstring>

// PRNG state: xorshift64*  (non-zero)
static std::atomic<uint64_t> prng_state{0};

// Mix function to turn time/address/heap/cpu into a decent seed
static uint64_t mix_seed(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    x = x ^ (x >> 31);
    if (x == 0) x = 0xF39A3E2DULL; // ensure non-zero
    return x;
}

// Ensure PRNG seeded once (thread-safe-ish using atomic compare_exchange)
// Improved seed mixes several varying sources so seed differs very early after boot.
static void seed_prng_once() {
    uint64_t cur = prng_state.load(std::memory_order_relaxed);
    if (cur != 0) return; // already seeded

    // Gather several sources of varying entropy that are portable across ESP targets
    uint64_t t = (uint64_t)esp_timer_get_time();              // microseconds since boot
    uint64_t free_heap = (uint64_t)esp_get_free_heap_size();  // free heap at the moment
    uint64_t a = (uint64_t)(uintptr_t)&t;                     // address of stack var (ASLR-ish entropy)

    // Try to incorporate esp_random/esp_fill_random if available at link time
    // We call it weakly: if not present the linker will remove the code path.
    uint32_t hwrand = 0;
#ifdef __has_include
#if __has_include("esp_system.h")
#include "esp_system.h"
#endif
#endif
#ifdef esp_fill_random
    // prefer esp_fill_random if available
    esp_fill_random(&hwrand, sizeof(hwrand));
#else
    // hardware RNG might not be available in this build; keep hwrand == 0
    (void)hwrand;
#endif

    // Mix bits together
    uint64_t raw = t ^ ((uint64_t)free_heap << 7) ^ (a << 11) ^ ((uint64_t)hwrand << 17);

    // Also mix a few bytes from stack to increase early-boot variability
    uint8_t stack_mix[8];
    // Try to fill with address- and time-derived bytes
    uint64_t m = raw;
    for (size_t i = 0; i < sizeof(stack_mix); ++i) {
        stack_mix[i] = (uint8_t)(m & 0xFF);
        m = (m >> 8) ^ (m << 5);
    }
    // Fold stack_mix into raw
    uint64_t fold = 0;
    for (size_t i = 0; i < sizeof(stack_mix); ++i) {
        fold = (fold << 8) ^ (uint64_t)stack_mix[i];
    }
    raw ^= fold;

    uint64_t seed = mix_seed(raw);

    uint64_t expected = 0;
    prng_state.compare_exchange_strong(expected, seed, std::memory_order_release, std::memory_order_relaxed);
}

// xorshift64* step. Uses atomic read+CAS to update state lock-free.
static uint64_t xorshift64star_next()
{
    seed_prng_once();
    while (true) {
        uint64_t s = prng_state.load(std::memory_order_acquire);
        uint64_t x = s;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        uint64_t next = x;
        uint64_t result = next * 0x2545F4914F6CDD1DULL;
        if (prng_state.compare_exchange_weak(s, next, std::memory_order_acq_rel, std::memory_order_acquire)) {
            return result;
        }
        // otherwise retry
    }
}

// Public: return 32-bit random value
static inline uint32_t get_rand32()
{
    uint64_t v = xorshift64star_next();
    return (uint32_t)(v & 0xFFFFFFFFu);
}

// Return a uniform random integer in [min_val, max_val] inclusive, unbiased via rejection sampling
size_t uniform_random_range(size_t min_val, size_t max_val)
{
    if (min_val >= max_val) return min_val;
    uint64_t range = (uint64_t)max_val - (uint64_t)min_val + 1ULL;
    const uint64_t TWO32 = (uint64_t)UINT64_C(0x100000000);
    uint64_t limit = (TWO32 / range) * range;
    uint64_t r;
    do {
        r = (uint64_t)get_rand32();
    } while (r >= limit);
    return (size_t)(min_val + (r % range));
}

// Fisher–Yates shuffle using the internal PRNG
void shuffle_strings(std::vector<std::string> &v)
{
    if (v.empty()) return;
    // seed_prng_once will be called inside get_rand32
    for (size_t i = v.size(); i > 1; --i) {
        size_t j = uniform_random_range(0, i - 1);
        if (j != (i - 1)) std::swap(v[i - 1], v[j]);
    }
}