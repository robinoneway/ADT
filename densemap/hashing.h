#pragma once
#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>
#include <utility>
#include "common/endian.h"
class hash_code {
    size_t value;

public:
    /// Default construct a hash_code.
    /// Note that this leaves the value uninitialized.
    hash_code() = default;

    /// Form a hash code directly from a numerical value.
    hash_code(size_t value) : value(value) {}

    /// Convert the hash code to its numerical value for use.
    /*explicit*/ operator size_t() const { return value; }

    friend bool operator==(const hash_code &lhs, const hash_code &rhs) {
        return lhs.value == rhs.value;
    }
    friend bool operator!=(const hash_code &lhs, const hash_code &rhs) {
        return lhs.value != rhs.value;
    }

    /// Allow a hash_code to be directly run through hash_value.
    friend size_t hash_value(const hash_code &code) { return code.value; }
};

template <typename T>
typename std::enable_if<std::is_integral<T>::value, hash_code>::type hash_value(
    T value);

template <typename T>
hash_code hash_value(const T *ptr);

template <typename T, typename U>
hash_code hash_value(const std::pair<T, U> &arg);

template <typename T>
hash_code hash_value(const std::basic_string<T> &arg);

void set_fixed_execution_hash_seed(uint64_t fixed_value);

namespace hashing {
namespace detail {

inline uint64_t fetch64(const char *p) {
    uint64_t result;
    std::memcpy(&result, p, sizeof(result));
    if (sys::IsBigEndianHost) sys::swapByteOrder(result);
    return result;
}

inline uint32_t fetch32(const char *p) {
    uint32_t result;
    memcpy(&result, p, sizeof(result));
    if (sys::IsBigEndianHost) sys::swapByteOrder(result);
    return result;
}

/// Some primes between 2^63 and 2^64 for various uses.
static const uint64_t k0 = 0xc3a5c85c97cb3127ULL;
static const uint64_t k1 = 0xb492b66fbe98f273ULL;
static const uint64_t k2 = 0x9ae16a3b2f90404fULL;
static const uint64_t k3 = 0xc949d7c7509e6557ULL;

inline uint64_t rotate(uint64_t val, size_t shift) {
    // Avoid shifting by 64: doing so yields an undefined result.
    return shift == 0 ? val : ((val >> shift) | (val << (64 - shift)));
}

inline uint64_t shift_mix(uint64_t val) { return val ^ (val >> 47); }

inline uint64_t hash_16_bytes(uint64_t low, uint64_t high) {
    // Murmur-inspired hashing.
    const uint64_t kMul = 0x9ddfea08eb382d69ULL;
    uint64_t a = (low ^ high) * kMul;
    a ^= (a >> 47);
    uint64_t b = (high ^ a) * kMul;
    b ^= (b >> 47);
    b *= kMul;
    return b;
}

inline uint64_t hash_1to3_bytes(const char *s, size_t len, uint64_t seed) {
    uint8_t a = s[0];
    uint8_t b = s[len >> 1];
    uint8_t c = s[len - 1];
    uint32_t y = static_cast<uint32_t>(a) + (static_cast<uint32_t>(b) << 8);
    uint32_t z = len + (static_cast<uint32_t>(c) << 2);
    return shift_mix(y * k2 ^ z * k3 ^ seed) * k2;
}

inline uint64_t hash_4to8_bytes(const char *s, size_t len, uint64_t seed) {
    uint64_t a = fetch32(s);
    return hash_16_bytes(len + (a << 3), seed ^ fetch32(s + len - 4));
}

inline uint64_t hash_9to16_bytes(const char *s, size_t len, uint64_t seed) {
    uint64_t a = fetch64(s);
    uint64_t b = fetch64(s + len - 8);
    return hash_16_bytes(seed ^ a, rotate(b + len, len)) ^ b;
}

inline uint64_t hash_17to32_bytes(const char *s, size_t len, uint64_t seed) {
    uint64_t a = fetch64(s) * k1;
    uint64_t b = fetch64(s + 8);
    uint64_t c = fetch64(s + len - 8) * k2;
    uint64_t d = fetch64(s + len - 16) * k0;
    return hash_16_bytes(rotate(a - b, 43) + rotate(c ^ seed, 30) + d,
                         a + rotate(b ^ k3, 20) - c + len + seed);
}

inline uint64_t hash_33to64_bytes(const char *s, size_t len, uint64_t seed) {
    uint64_t z = fetch64(s + 24);
    uint64_t a = fetch64(s) + (len + fetch64(s + len - 16)) * k0;
    uint64_t b = rotate(a + z, 52);
    uint64_t c = rotate(a, 37);
    a += fetch64(s + 8);
    c += rotate(a, 7);
    a += fetch64(s + 16);
    uint64_t vf = a + z;
    uint64_t vs = b + rotate(a, 31) + c;
    a = fetch64(s + 16) + fetch64(s + len - 32);
    z = fetch64(s + len - 8);
    b = rotate(a + z, 52);
    c = rotate(a, 37);
    a += fetch64(s + len - 24);
    c += rotate(a, 7);
    a += fetch64(s + len - 16);
    uint64_t wf = a + z;
    uint64_t ws = b + rotate(a, 31) + c;
    uint64_t r = shift_mix((vf + ws) * k2 + (wf + vs) * k0);
    return shift_mix((seed ^ (r * k0)) + vs) * k2;
}

inline uint64_t hash_short(const char *s, size_t length, uint64_t seed) {
    if (length >= 4 && length <= 8) return hash_4to8_bytes(s, length, seed);
    if (length > 8 && length <= 16) return hash_9to16_bytes(s, length, seed);
    if (length > 16 && length <= 32) return hash_17to32_bytes(s, length, seed);
    if (length > 32) return hash_33to64_bytes(s, length, seed);
    if (length != 0) return hash_1to3_bytes(s, length, seed);

    return k2 ^ seed;
}

/// The intermediate state used during hashing.
/// Currently, the algorithm for computing hash codes is based on CityHash and
/// keeps 56 bytes of arbitrary state.
struct hash_state {
    uint64_t h0, h1, h2, h3, h4, h5, h6;

    /// Create a new hash_state structure and initialize it based on the
    /// seed and the first 64-byte chunk.
    /// This effectively performs the initial mix.
    static hash_state create(const char *s, uint64_t seed) {
        hash_state state = {0,
                            seed,
                            hash_16_bytes(seed, k1),
                            rotate(seed ^ k1, 49),
                            seed * k1,
                            shift_mix(seed),
                            0};
        state.h6 = hash_16_bytes(state.h4, state.h5);
        state.mix(s);
        return state;
    }

    /// Mix 32-bytes from the input sequence into the 16-bytes of 'a'
    /// and 'b', including whatever is already in 'a' and 'b'.
    static void mix_32_bytes(const char *s, uint64_t &a, uint64_t &b) {
        a += fetch64(s);
        uint64_t c = fetch64(s + 24);
        b = rotate(b + a + c, 21);
        uint64_t d = a;
        a += fetch64(s + 8) + fetch64(s + 16);
        b += rotate(a, 44) + d;
        a += c;
    }

    /// Mix in a 64-byte buffer of data.
    /// We mix all 64 bytes even when the chunk length is smaller, but we
    /// record the actual length.
    void mix(const char *s) {
        h0 = rotate(h0 + h1 + h3 + fetch64(s + 8), 37) * k1;
        h1 = rotate(h1 + h4 + fetch64(s + 48), 42) * k1;
        h0 ^= h6;
        h1 += h3 + fetch64(s + 40);
        h2 = rotate(h2 + h5, 33) * k1;
        h3 = h4 * k1;
        h4 = h0 + h5;
        mix_32_bytes(s, h3, h4);
        h5 = h2 + h6;
        h6 = h1 + fetch64(s + 16);
        mix_32_bytes(s + 32, h5, h6);
        std::swap(h2, h0);
    }

    /// Compute the final 64-bit hash code value based on the current
    /// state and the length of bytes hashed.
    uint64_t finalize(size_t length) {
        return hash_16_bytes(
            hash_16_bytes(h3, h5) + shift_mix(h1) * k1 + h2,
            hash_16_bytes(h4, h6) + shift_mix(length) * k1 + h0);
    }
};

uint64_t fixed_seed_override = 0;

inline uint64_t get_execution_seed() {
    const uint64_t seed_prime = 0xff51afd7ed558ccdULL;
    static uint64_t seed =
        fixed_seed_override ? fixed_seed_override : seed_prime;
    return seed;
}

template <typename T>
struct is_hashable_data
    : std::integral_constant<bool, ((std::is_integral<T>::value ||
                                     std::is_pointer<T>::value) &&
                                    64 % sizeof(T) == 0)> {};

template <typename T, typename U>
struct is_hashable_data<std::pair<T, U>>
    : std::integral_constant<
          bool, (is_hashable_data<T>::value && is_hashable_data<U>::value &&
                 (sizeof(T) + sizeof(U)) == sizeof(std::pair<T, U>))> {};

/// Helper to get the hashable data representation for a type.
/// This variant is enabled when the type itself can be used.
template <typename T>
typename std::enable_if<is_hashable_data<T>::value, T>::type get_hashable_data(
    const T &value) {
    return value;
}
template <typename T>
typename std::enable_if<!is_hashable_data<T>::value, size_t>::type
get_hashable_data(const T &value) {
    return hash_value(value);
}

template <typename T>
bool store_and_advance(char *&buffer_ptr, char *buffer_end, const T &value,
                       size_t offset = 0) {
    size_t store_size = sizeof(value) - offset;
    if (buffer_ptr + store_size > buffer_end) return false;
    const char *value_data = reinterpret_cast<const char *>(&value);
    memcpy(buffer_ptr, value_data + offset, store_size);
    buffer_ptr += store_size;
    return true;
}

template <typename InputIteratorT>
hash_code hash_combine_range_impl(InputIteratorT first, InputIteratorT last) {
    const uint64_t seed = get_execution_seed();
    char buffer[64], *buffer_ptr = buffer;
    char *const buffer_end = std::end(buffer);
    while (first != last &&
           store_and_advance(buffer_ptr, buffer_end, get_hashable_data(*first)))
        ++first;
    if (first == last) return hash_short(buffer, buffer_ptr - buffer, seed);
    assert(buffer_ptr == buffer_end);

    hash_state state = state.create(buffer, seed);
    size_t length = 64;
    while (first != last) {
        // Fill up the buffer. We don't clear it, which re-mixes the last round
        // when only a partial 64-byte chunk is left.
        buffer_ptr = buffer;
        while (first != last && store_and_advance(buffer_ptr, buffer_end,
                                                  get_hashable_data(*first)))
            ++first;

        // Rotate the buffer if we did a partial fill in order to simulate doing
        // a mix of the last 64-bytes. That is how the algorithm works when we
        // have a contiguous byte sequence, and we want to emulate that here.
        std::rotate(buffer, buffer_ptr, buffer_end);

        // Mix this chunk into the current state.
        state.mix(buffer);
        length += buffer_ptr - buffer;
    };

    return state.finalize(length);
}

template <typename ValueT>
typename std::enable_if<is_hashable_data<ValueT>::value, hash_code>::type
hash_combine_range_impl(ValueT *first, ValueT *last) {
    const uint64_t seed = get_execution_seed();
    const char *s_begin = reinterpret_cast<const char *>(first);
    const char *s_end = reinterpret_cast<const char *>(last);
    const size_t length = std::distance(s_begin, s_end);
    if (length <= 64) return hash_short(s_begin, length, seed);

    const char *s_aligned_end = s_begin + (length & ~63);
    hash_state state = state.create(s_begin, seed);
    s_begin += 64;
    while (s_begin != s_aligned_end) {
        state.mix(s_begin);
        s_begin += 64;
    }
    if (length & 63) state.mix(s_end - 64);

    return state.finalize(length);
}

}  // namespace detail
}  // namespace hashing

template <typename InputIteratorT>
hash_code hash_combine_range(InputIteratorT first, InputIteratorT last) {
    return ::hashing::detail::hash_combine_range_impl(first, last);
}

// Implementation details for hash_combine.
namespace hashing {
namespace detail {

struct hash_combine_recursive_helper {
    char buffer[64];
    hash_state state;
    const uint64_t seed;

public:
    hash_combine_recursive_helper() : seed(get_execution_seed()) {}
    template <typename T>
    char *combine_data(size_t &length, char *buffer_ptr, char *buffer_end,
                       T data) {
        if (!store_and_advance(buffer_ptr, buffer_end, data)) {
            size_t partial_store_size = buffer_end - buffer_ptr;
            memcpy(buffer_ptr, &data, partial_store_size);

            if (length == 0) {
                state = state.create(buffer, seed);
                length = 64;
            } else {
                // Mix this chunk into the current state and bump length up
                // by 64.
                state.mix(buffer);
                length += 64;
            }
            // Reset the buffer_ptr to the head of the buffer for the next chunk
            // of data.
            buffer_ptr = buffer;

            // Try again to store into the buffer -- this cannot fail as we only
            // store types smaller than the buffer.
            if (!store_and_advance(buffer_ptr, buffer_end, data,
                                   partial_store_size))
                abort();
        }
        return buffer_ptr;
    }

    /// Recursive, variadic combining method.
    ///
    /// This function recurses through each argument, combining that argument
    /// into a single hash.
    template <typename T, typename... Ts>
    hash_code combine(size_t length, char *buffer_ptr, char *buffer_end,
                      const T &arg, const Ts &... args) {
        buffer_ptr = combine_data(length, buffer_ptr, buffer_end,
                                  get_hashable_data(arg));

        // Recurse to the next argument.
        return combine(length, buffer_ptr, buffer_end, args...);
    }

    hash_code combine(size_t length, char *buffer_ptr, char *buffer_end) {
        if (length == 0) return hash_short(buffer, buffer_ptr - buffer, seed);

        std::rotate(buffer, buffer_ptr, buffer_end);

        state.mix(buffer);
        length += buffer_ptr - buffer;

        return state.finalize(length);
    }
};

}  // namespace detail
}  // namespace hashing

template <typename... Ts>
hash_code hash_combine(const Ts &... args) {
    // Recursively hash each argument using a helper class.
    ::hashing::detail::hash_combine_recursive_helper helper;
    return helper.combine(0, helper.buffer, helper.buffer + 64, args...);
}

// Implementation details for implementations of hash_value overloads provided
// here.
namespace hashing {
namespace detail {

/// Helper to hash the value of a single integer.
///
/// Overloads for smaller integer types are not provided to ensure consistent
/// behavior in the presence of integral promotions. Essentially,
/// "hash_value('4')" and "hash_value('0' + 4)" should be the same.
inline hash_code hash_integer_value(uint64_t value) {
    // Similar to hash_4to8_bytes but using a seed instead of length.
    const uint64_t seed = get_execution_seed();
    const char *s = reinterpret_cast<const char *>(&value);
    const uint64_t a = fetch32(s);
    return hash_16_bytes(seed + (a << 3), fetch32(s + 4));
}

}  // namespace detail
}  // namespace hashing

template <typename T>
typename std::enable_if<std::is_integral<T>::value, hash_code>::type hash_value(
    T value) {
    return ::hashing::detail::hash_integer_value(static_cast<uint64_t>(value));
}

// Declared and documented above, but defined here so that any of the hashing
// infrastructure is available.
template <typename T>
hash_code hash_value(const T *ptr) {
    return ::hashing::detail::hash_integer_value(
        reinterpret_cast<uintptr_t>(ptr));
}

// Declared and documented above, but defined here so that any of the hashing
// infrastructure is available.
template <typename T, typename U>
hash_code hash_value(const std::pair<T, U> &arg) {
    return hash_combine(arg.first, arg.second);
}

// Declared and documented above, but defined here so that any of the hashing
// infrastructure is available.
template <typename T>
hash_code hash_value(const std::basic_string<T> &arg) {
    return hash_combine_range(arg.begin(), arg.end());
}
