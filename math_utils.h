#pragma once
namespace detail {
template <typename T, std::size_t SizeOfT>
struct LeadingZerosCounter {
    static std::size_t count(T Val) {
        if (!Val) return std::numeric_limits<T>::digits;
        // Bisection method.
        std::size_t ZeroBits = 0;
        for (T Shift = std::numeric_limits<T>::digits >> 1; Shift;
             Shift >>= 1) {
            T Tmp = Val >> Shift;
            if (Tmp)
                Val = Tmp;
            else
                ZeroBits |= Shift;
        }
        return ZeroBits;
    }
};
#if __GNUC__ >= 4 || defined(_MSC_VER)
template <typename T>
struct LeadingZerosCounter<T, 4> {
    static std::size_t count(T Val) {
        if (Val == 0) return 32;
#if defined(_MSC_VER)
        unsigned long Index;
        _BitScanReverse(&Index, Val);
        return Index ^ 31;
#else
        return __builtin_clz(Val);
#endif
    }
};
#if !defined(_MSC_VER) || defined(_M_X64)
template <typename T>
struct LeadingZerosCounter<T, 8> {
    static std::size_t count(T Val) {
        if (Val == 0) return 64;
#if defined(_MSC_VER)
        unsigned long Index;
        _BitScanReverse64(&Index, Val);
        return Index ^ 63;
#else
        return __builtin_clzll(Val);
#endif
    }
};
#endif
#endif
}  // namespace detail

inline uint64_t NextPowerOf2(uint64_t A) {
    A |= (A >> 1);
    A |= (A >> 2);
    A |= (A >> 4);
    A |= (A >> 8);
    A |= (A >> 16);
    A |= (A >> 32);
    return A + 1;
}
template <typename T>
std::size_t countLeadingZeros(T Val) {
    static_assert(std::numeric_limits<T>::is_integer &&
                      !std::numeric_limits<T>::is_signed,
                  "Only unsigned integral types are allowed.");
    return ::detail::LeadingZerosCounter<T, sizeof(T)>::count(Val);
}
inline unsigned Log2_32_Ceil(uint32_t Value) {
    return 32 - countLeadingZeros(Value - 1);
}
constexpr inline bool isPowerOf2_32(uint32_t Value) {
    return Value && !(Value & (Value - 1));
}

constexpr inline bool isPowerOf2_64(uint64_t Value) {
    return Value && !(Value & (Value - 1));
}
