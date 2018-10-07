#pragma once
#include <cstddef>
#include <cstdint>
#include <utility>
#include "hashing.h"
#include "type_traits.h"
template <typename T>
struct HashMapInfo {
    // static inline T GetEmptyKey();
    // static inline T GetTombstoneKey();
    // static unsigned GetHashValue(const T &Val);
    // static bool IsEqual(const T &lhs, const T &rhs);
};

// Provide HashMapInfo for all pointers.
template <typename T>
struct HashMapInfo<T*> {
    static inline T* GetEmptyKey() {
        uintptr_t Val = static_cast<uintptr_t>(-1);
        Val <<= PointerLikeTypeTraits<T*>::NumLowBitsAvailable;
        return reinterpret_cast<T*>(Val);
    }

    static inline T* GetTombstoneKey() {
        uintptr_t Val = static_cast<uintptr_t>(-2);
        Val <<= PointerLikeTypeTraits<T*>::NumLowBitsAvailable;
        return reinterpret_cast<T*>(Val);
    }

    static unsigned GetHashValue(const T* PtrVal) {
        return (unsigned((uintptr_t)PtrVal) >> 4) ^
               (unsigned((uintptr_t)PtrVal) >> 9);
    }

    static bool IsEqual(const T* lhs, const T* rhs) { return lhs == rhs; }
};

// Provide HashMapInfo for chars.
template <>
struct HashMapInfo<char> {
    static inline char GetEmptyKey() { return ~0; }
    static inline char GetTombstoneKey() { return ~0 - 1; }
    static unsigned GetHashValue(const char& Val) { return Val * 37U; }

    static bool IsEqual(const char& lhs, const char& rhs) { return lhs == rhs; }
};

// Provide HashMapInfo for unsigned shorts.
template <>
struct HashMapInfo<unsigned short> {
    static inline unsigned short GetEmptyKey() { return 0xFFFF; }
    static inline unsigned short GetTombstoneKey() { return 0xFFFF - 1; }
    static unsigned GetHashValue(const unsigned short& Val) {
        return Val * 37U;
    }

    static bool IsEqual(const unsigned short& lhs, const unsigned short& rhs) {
        return lhs == rhs;
    }
};

// Provide HashMapInfo for unsigned ints.
template <>
struct HashMapInfo<unsigned> {
    static inline unsigned GetEmptyKey() { return ~0U; }
    static inline unsigned GetTombstoneKey() { return ~0U - 1; }
    static unsigned GetHashValue(const unsigned& Val) { return Val * 37U; }

    static bool IsEqual(const unsigned& lhs, const unsigned& rhs) {
        return lhs == rhs;
    }
};

// Provide HashMapInfo for unsigned longs.
template <>
struct HashMapInfo<unsigned long> {
    static inline unsigned long GetEmptyKey() { return ~0UL; }
    static inline unsigned long GetTombstoneKey() { return ~0UL - 1L; }

    static unsigned GetHashValue(const unsigned long& Val) {
        return (unsigned)(Val * 37UL);
    }

    static bool IsEqual(const unsigned long& lhs, const unsigned long& rhs) {
        return lhs == rhs;
    }
};

// Provide HashMapInfo for unsigned long longs.
template <>
struct HashMapInfo<unsigned long long> {
    static inline unsigned long long GetEmptyKey() { return ~0ULL; }
    static inline unsigned long long GetTombstoneKey() { return ~0ULL - 1ULL; }

    static unsigned GetHashValue(const unsigned long long& Val) {
        return (unsigned)(Val * 37ULL);
    }

    static bool IsEqual(const unsigned long long& lhs,
                        const unsigned long long& rhs) {
        return lhs == rhs;
    }
};

// Provide HashMapInfo for shorts.
template <>
struct HashMapInfo<short> {
    static inline short GetEmptyKey() { return 0x7FFF; }
    static inline short GetTombstoneKey() { return -0x7FFF - 1; }
    static unsigned GetHashValue(const short& Val) { return Val * 37U; }
    static bool IsEqual(const short& lhs, const short& rhs) {
        return lhs == rhs;
    }
};

// Provide HashMapInfo for ints.
template <>
struct HashMapInfo<int> {
    static inline int GetEmptyKey() { return 0x7fffffff; }
    static inline int GetTombstoneKey() { return -0x7fffffff - 1; }
    static unsigned GetHashValue(const int& Val) {
        return (unsigned)(Val * 37U);
    }

    static bool IsEqual(const int& lhs, const int& rhs) { return lhs == rhs; }
};

// Provide HashMapInfo for longs.
template <>
struct HashMapInfo<long> {
    static inline long GetEmptyKey() {
        return (1UL << (sizeof(long) * 8 - 1)) - 1UL;
    }

    static inline long GetTombstoneKey() { return GetEmptyKey() - 1L; }

    static unsigned GetHashValue(const long& Val) {
        return (unsigned)(Val * 37UL);
    }

    static bool IsEqual(const long& lhs, const long& rhs) { return lhs == rhs; }
};

// Provide HashMapInfo for long longs.
template <>
struct HashMapInfo<long long> {
    static inline long long GetEmptyKey() { return 0x7fffffffffffffffLL; }
    static inline long long GetTombstoneKey() {
        return -0x7fffffffffffffffLL - 1;
    }

    static unsigned GetHashValue(const long long& Val) {
        return (unsigned)(Val * 37ULL);
    }

    static bool IsEqual(const long long& lhs, const long long& rhs) {
        return lhs == rhs;
    }
};

// Provide HashMapInfo for all pairs whose members have info.
template <typename T, typename U>
struct HashMapInfo<std::pair<T, U>> {
    using Pair = std::pair<T, U>;
    using FirstInfo = HashMapInfo<T>;
    using SecondInfo = HashMapInfo<U>;

    static inline Pair GetEmptyKey() {
        return std::make_pair(FirstInfo::GetEmptyKey(),
                              SecondInfo::GetEmptyKey());
    }

    static inline Pair GetTombstoneKey() {
        return std::make_pair(FirstInfo::GetTombstoneKey(),
                              SecondInfo::GetTombstoneKey());
    }

    static unsigned GetHashValue(const Pair& PairVal) {
        uint64_t key = (uint64_t)FirstInfo::GetHashValue(PairVal.first) << 32 |
                       (uint64_t)SecondInfo::GetHashValue(PairVal.second);
        key += ~(key << 32);
        key ^= (key >> 22);
        key += ~(key << 13);
        key ^= (key >> 8);
        key += (key << 3);
        key ^= (key >> 15);
        key += ~(key << 27);
        key ^= (key >> 31);
        return (unsigned)key;
    }

    static bool IsEqual(const Pair& lhs, const Pair& rhs) {
        return FirstInfo::IsEqual(lhs.first, rhs.first) &&
               SecondInfo::IsEqual(lhs.second, rhs.second);
    }
};

// Provide HashMapInfo for std::strings.
template <>
struct HashMapInfo<std::string> {
    static inline std::string GetEmptyKey() {
        return std::string(
            reinterpret_cast<const char*>(~static_cast<uintptr_t>(0)), 0);
    }

    static inline std::string GetTombstoneKey() {
        return std::string(
            reinterpret_cast<const char*>(~static_cast<uintptr_t>(1)), 0);
    }

    static unsigned GetHashValue(std::string Val) {
        assert(Val.data() != GetEmptyKey().data() &&
               "Cannot hash the empty key!");
        assert(Val.data() != GetTombstoneKey().data() &&
               "Cannot hash the tombstone key!");
        return (unsigned)(hash_value(Val));
    }

    static bool IsEqual(std::string lhs, std::string rhs) {
        if (rhs.data() == GetEmptyKey().data())
            return lhs.data() == GetEmptyKey().data();
        if (rhs.data() == GetTombstoneKey().data())
            return lhs.data() == GetTombstoneKey().data();
        return lhs == rhs;
    }
};

template <>
struct HashMapInfo<hash_code> {
    static inline hash_code GetEmptyKey() { return hash_code(-1); }
    static inline hash_code GetTombstoneKey() { return hash_code(-2); }
    static unsigned GetHashValue(hash_code val) { return val; }
    static bool IsEqual(hash_code lhs, hash_code rhs) { return lhs == rhs; }
};
