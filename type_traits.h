#include <type_traits>
#include <utility>
template <typename T>
struct isPodLike {
#if (defined(__GNUC__) && __GNUC__ >= 5)
    // If the compiler supports the is_trivially_copyable trait use it, as it
    // matches the definition of isPodLike closely.
    static const bool value = std::is_trivially_copyable<T>::value;
#else
    static const bool value = !std::is_class<T>::value;
#endif
};

// std::pair's are pod-like if their elements are.
template <typename T, typename U>
struct isPodLike<std::pair<T, U>> {
    static const bool value = isPodLike<T>::value && isPodLike<U>::value;
};

template <typename T>
class is_integral_or_enum {
    using UnderlyingT = typename std::remove_reference<T>::type;

   public:
    static const bool value =
        !std::is_class<UnderlyingT>::value &&  // Filter conversion operators.
        !std::is_pointer<UnderlyingT>::value &&
        !std::is_floating_point<UnderlyingT>::value &&
        (std::is_enum<UnderlyingT>::value ||
         std::is_convertible<UnderlyingT, unsigned long long>::value);
};

/// If T is a pointer, just return it. If it is not, return T&.
template <typename T, typename Enable = void>
struct add_lvalue_reference_if_not_pointer {
    using type = T &;
};

template <typename T>
struct add_lvalue_reference_if_not_pointer<
    T, typename std::enable_if<std::is_pointer<T>::value>::type> {
    using type = T;
};

template <typename T, typename Enable = void>
struct add_const_past_pointer {
    using type = const T;
};

template <typename T>
struct add_const_past_pointer<
    T, typename std::enable_if<std::is_pointer<T>::value>::type> {
    using type = const typename std::remove_pointer<T>::type *;
};

template <typename T, typename Enable = void>
struct const_pointer_or_const_ref {
    using type = const T &;
};
template <typename T>
struct const_pointer_or_const_ref<
    T, typename std::enable_if<std::is_pointer<T>::value>::type> {
    using type = typename add_const_past_pointer<T>::type;
};

namespace detail {
/// Internal utility to detect trivial copy construction.
template <typename T>
union copy_construction_triviality_helper {
    T t;
    copy_construction_triviality_helper() = default;
    copy_construction_triviality_helper(
        const copy_construction_triviality_helper &) = default;
    ~copy_construction_triviality_helper() = default;
};
/// Internal utility to detect trivial move construction.
template <typename T>
union move_construction_triviality_helper {
    T t;
    move_construction_triviality_helper() = default;
    move_construction_triviality_helper(
        move_construction_triviality_helper &&) = default;
    ~move_construction_triviality_helper() = default;
};
}  // end namespace detail

/// An implementation of `std::is_trivially_copy_constructible` since we have
/// users with STLs that don't yet include it.
template <typename T>
struct is_trivially_copy_constructible
    : std::is_copy_constructible<
          ::detail::copy_construction_triviality_helper<T>> {};
template <typename T>
struct is_trivially_copy_constructible<T &> : std::true_type {};
template <typename T>
struct is_trivially_copy_constructible<T &&> : std::false_type {};

/// An implementation of `std::is_trivially_move_constructible` since we have
/// users with STLs that don't yet include it.
template <typename T>
struct is_trivially_move_constructible
    : std::is_move_constructible<
          ::detail::move_construction_triviality_helper<T>> {};
template <typename T>
struct is_trivially_move_constructible<T &> : std::true_type {};
template <typename T>
struct is_trivially_move_constructible<T &&> : std::true_type {};

/// A traits type that is used to handle pointer types and things that are just
/// wrappers for pointers as a uniform entity.
template <typename T>
struct PointerLikeTypeTraits;

namespace detail {
/// A tiny meta function to compute the log2 of a compile time constant.
template <size_t N>
struct ConstantLog2
    : std::integral_constant<size_t, ConstantLog2<N / 2>::value + 1> {};
template <>
struct ConstantLog2<1> : std::integral_constant<size_t, 0> {};

// Provide a trait to check if T is pointer-like.
template <typename T, typename U = void>
struct HasPointerLikeTypeTraits {
    static const bool value = false;
};

// sizeof(T) is valid only for a complete T.
template <typename T>
struct HasPointerLikeTypeTraits<
    T, decltype((sizeof(PointerLikeTypeTraits<T>) + sizeof(T)), void())> {
    static const bool value = true;
};

template <typename T>
struct IsPointerLike {
    static const bool value = HasPointerLikeTypeTraits<T>::value;
};

template <typename T>
struct IsPointerLike<T *> {
    static const bool value = true;
};
}  // namespace detail

// Provide PointerLikeTypeTraits for non-cvr pointers.
template <typename T>
struct PointerLikeTypeTraits<T *> {
    static inline void *getAsVoidPointer(T *P) { return P; }
    static inline T *getFromVoidPointer(void *P) { return static_cast<T *>(P); }

    enum { NumLowBitsAvailable = detail::ConstantLog2<alignof(T)>::value };
};

template <>
struct PointerLikeTypeTraits<void *> {
    static inline void *getAsVoidPointer(void *P) { return P; }
    static inline void *getFromVoidPointer(void *P) { return P; }

    enum { NumLowBitsAvailable = 2 };
};

// Provide PointerLikeTypeTraits for const things.
template <typename T>
struct PointerLikeTypeTraits<const T> {
    typedef PointerLikeTypeTraits<T> NonConst;

    static inline const void *getAsVoidPointer(const T P) {
        return NonConst::getAsVoidPointer(P);
    }
    static inline const T getFromVoidPointer(const void *P) {
        return NonConst::getFromVoidPointer(const_cast<void *>(P));
    }
    enum { NumLowBitsAvailable = NonConst::NumLowBitsAvailable };
};

// Provide PointerLikeTypeTraits for const pointers.
template <typename T>
struct PointerLikeTypeTraits<const T *> {
    typedef PointerLikeTypeTraits<T *> NonConst;

    static inline const void *getAsVoidPointer(const T *P) {
        return NonConst::getAsVoidPointer(const_cast<T *>(P));
    }
    static inline const T *getFromVoidPointer(const void *P) {
        return NonConst::getFromVoidPointer(const_cast<void *>(P));
    }
    enum { NumLowBitsAvailable = NonConst::NumLowBitsAvailable };
};

// Provide PointerLikeTypeTraits for uintptr_t.
template <>
struct PointerLikeTypeTraits<uintptr_t> {
    static inline void *getAsVoidPointer(uintptr_t P) {
        return reinterpret_cast<void *>(P);
    }
    static inline uintptr_t getFromVoidPointer(void *P) {
        return reinterpret_cast<uintptr_t>(P);
    }
    // No bits are available!
    enum { NumLowBitsAvailable = 0 };
};

template <int Alignment, typename FunctionPointerT>
struct FunctionPointerLikeTypeTraits {
    enum { NumLowBitsAvailable = detail::ConstantLog2<Alignment>::value };
    static inline void *getAsVoidPointer(FunctionPointerT P) {
        assert((reinterpret_cast<uintptr_t>(P) &
                ~((uintptr_t)-1 << NumLowBitsAvailable)) == 0 &&
               "Alignment not satisfied for an actual function pointer!");
        return reinterpret_cast<void *>(P);
    }
    static inline FunctionPointerT getFromVoidPointer(void *P) {
        return reinterpret_cast<FunctionPointerT>(P);
    }
};

template <typename ReturnT, typename... ParamTs>
struct PointerLikeTypeTraits<ReturnT (*)(ParamTs...)>
    : FunctionPointerLikeTypeTraits<4, ReturnT (*)(ParamTs...)> {};
