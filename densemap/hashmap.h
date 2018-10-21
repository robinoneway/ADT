#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <new>
#include <type_traits>
#include <utility>

#include "common/alignof.h"
#include "common/math_utils.h"
#include "densemap/hashmap_info.h"

namespace detail {

template <typename KeyT, typename ValueT>
struct HashMapPair : public std::pair<KeyT, ValueT> {
    KeyT &GetFirst() { return std::pair<KeyT, ValueT>::first; }
    const KeyT &GetFirst() const { return std::pair<KeyT, ValueT>::first; }
    ValueT &GetSecond() { return std::pair<KeyT, ValueT>::second; }
    const ValueT &GetSecond() const { return std::pair<KeyT, ValueT>::second; }
};

}  // end namespace detail

template <typename KeyT, typename ValueT, typename KeyInfoT = HashMapInfo<KeyT>,
          typename Bucket = detail::HashMapPair<KeyT, ValueT>,
          bool IsConst = false>
class HashMapIterator;

template <typename DerivedT, typename KeyT, typename ValueT, typename KeyInfoT,
          typename BucketT>
class HashMapBase {
    template <typename T>
    using const_arg_type_t = typename const_pointer_or_const_ref<T>::type;

public:
    using size_type = unsigned;
    using key_type = KeyT;
    using mapped_type = ValueT;
    using value_type = BucketT;

    using iterator = HashMapIterator<KeyT, ValueT, KeyInfoT, BucketT>;
    using const_iterator =
        HashMapIterator<KeyT, ValueT, KeyInfoT, BucketT, true>;

    inline iterator begin() {
        // When the map is empty, avoid the overhead of advancing/retreating
        // past empty buckets.
        if (empty()) return end();
        return MakeIterator(getBuckets(), getBucketsend());
    }
    inline iterator end() {
        return MakeIterator(getBucketsend(), getBucketsend(), true);
    }
    inline const_iterator begin() const {
        if (empty()) return end();
        return MakeConstIterator(getBuckets(), getBucketsend());
    }
    inline const_iterator end() const {
        return MakeConstIterator(getBucketsend(), getBucketsend(), true);
    }

    bool empty() const { return num_entries() == 0; }
    unsigned size() const { return num_entries(); }

    void reserve(size_type num_entries_) {
        auto num_buckets_ = getMinBucketToReserveForEntries(num_entries_);
        if (num_buckets_ > getNumBukets()) Grow(num_buckets_);
    }

    void clear() {
        if (num_entries() == 0 && num_to_mbstones() == 0) return;

        // If the capacity of the array is huge, and the # elements used is
        // small, shrink the array.
        if (num_entries() * 4 < getNumBukets() && getNumBukets() > 64) {
            shrink_and_clear();
            return;
        }

        const KeyT EmptyKey = GetEmptyKey(), TombstoneKey = GetTombstoneKey();
        if (std::is_pod<KeyT>::value && std::is_pod<ValueT>::value) {
            // Use a simpler loop when these are trivial types.
            for (BucketT *P = getBuckets(), *E = getBucketsend(); P != E; ++P)
                P->GetFirst() = EmptyKey;
        } else {
            unsigned num_entries_ = num_entries();
            for (BucketT *P = getBuckets(), *E = getBucketsend(); P != E; ++P) {
                if (!KeyInfoT::IsEqual(P->GetFirst(), EmptyKey)) {
                    if (!KeyInfoT::IsEqual(P->GetFirst(), TombstoneKey)) {
                        P->GetSecond().~ValueT();
                        --num_entries_;
                    }
                    P->GetFirst() = EmptyKey;
                }
            }
            assert(num_entries_ == 0 && "Node count imbalance!");
        }
        set_num_entries(0);
        set_num_to_mbstones(0);
    }

    /// Return 1 if the specified key is in the map, 0 otherwise.
    size_type count(const_arg_type_t<KeyT> Val) const {
        const BucketT *the_bucket_;
        return LookupBucketFor(Val, the_bucket_) ? 1 : 0;
    }

    iterator find(const_arg_type_t<KeyT> Val) {
        BucketT *the_bucket_;
        if (LookupBucketFor(Val, the_bucket_))
            return MakeIterator(the_bucket_, getBucketsend(), *this, true);
        return end();
    }
    const_iterator find(const_arg_type_t<KeyT> Val) const {
        const BucketT *the_bucket_;
        if (LookupBucketFor(Val, the_bucket_))
            return MakeConstIterator(the_bucket_, getBucketsend(), *this, true);
        return end();
    }

    /// Alternate version of find() which allows a different, and possibly
    /// less expensive, key type.
    /// The HashMapInfo is responsible for supplying methods
    /// GetHashValue(LookupKeyT) and IsEqual(LookupKeyT, KeyT) for each key
    /// type used.
    template <class LookupKeyT>
    iterator find_as(const LookupKeyT &Val) {
        BucketT *the_bucket_;
        if (LookupBucketFor(Val, the_bucket_))
            return MakeIterator(the_bucket_, getBucketsend(), true);
        return end();
    }
    template <class LookupKeyT>
    const_iterator find_as(const LookupKeyT &Val) const {
        const BucketT *the_bucket_;
        if (LookupBucketFor(Val, the_bucket_))
            return MakeConstIterator(the_bucket_, getBucketsend(), true);
        return end();
    }

    /// lookup - Return the entry for the specified key, or a default
    /// constructed value if no such entry exists.
    ValueT lookup(const_arg_type_t<KeyT> Val) const {
        const BucketT *the_bucket_;
        if (LookupBucketFor(Val, the_bucket_)) return the_bucket_->GetSecond();
        return ValueT();
    }

    // Inserts key,value pair into the map if the key isn't already in the map.
    // If the key is already in the map, it returns false and doesn't update the
    // value.
    std::pair<iterator, bool> insert(const std::pair<KeyT, ValueT> &KV) {
        return try_emplace(KV.first, KV.second);
    }

    // Inserts key,value pair into the map if the key isn't already in the map.
    // If the key is already in the map, it returns false and doesn't update the
    // value.
    std::pair<iterator, bool> insert(std::pair<KeyT, ValueT> &&KV) {
        return try_emplace(std::move(KV.first), std::move(KV.second));
    }

    // Inserts key,value pair into the map if the key isn't already in the map.
    // The value is constructed in-place if the key is not in the map, otherwise
    // it is not moved.
    template <typename... Ts>
    std::pair<iterator, bool> try_emplace(KeyT &&Key, Ts &&... Args) {
        BucketT *the_bucket_;
        if (LookupBucketFor(Key, the_bucket_))
            return std::make_pair(
                MakeIterator(the_bucket_, getBucketsend(), true),
                false);  // Already in map.

        // Otherwise, insert the new element.
        the_bucket_ = InsertIntoBucket(the_bucket_, std::move(Key),
                                       std::forward<Ts>(Args)...);
        return std::make_pair(MakeIterator(the_bucket_, getBucketsend(), true),
                              true);
    }

    // Inserts key,value pair into the map if the key isn't already in the map.
    // The value is constructed in-place if the key is not in the map, otherwise
    // it is not moved.
    template <typename... Ts>
    std::pair<iterator, bool> try_emplace(const KeyT &Key, Ts &&... Args) {
        BucketT *the_bucket_;
        if (LookupBucketFor(Key, the_bucket_))
            return std::make_pair(
                MakeIterator(the_bucket_, getBucketsend(), true),
                false);  // Already in map.

        // Otherwise, insert the new element.
        the_bucket_ =
            InsertIntoBucket(the_bucket_, Key, std::forward<Ts>(Args)...);
        return std::make_pair(MakeIterator(the_bucket_, getBucketsend(), true),
                              true);
    }

    template <typename LookupKeyT>
    std::pair<iterator, bool> insert_as(std::pair<KeyT, ValueT> &&KV,
                                        const LookupKeyT &Val) {
        BucketT *the_bucket_;
        if (LookupBucketFor(Val, the_bucket_))
            return std::make_pair(
                MakeIterator(the_bucket_, getBucketsend(), true),
                false);  // Already in map.

        // Otherwise, insert the new element.
        the_bucket_ = InsertIntoBucketWithLookup(
            the_bucket_, std::move(KV.first), std::move(KV.second), Val);
        return std::make_pair(MakeIterator(the_bucket_, getBucketsend(), true),
                              true);
    }

    /// insert - Range insertion of pairs.
    template <typename InputIt>
    void insert(InputIt I, InputIt E) {
        for (; I != E; ++I) insert(*I);
    }

    bool erase(const KeyT &Val) {
        BucketT *the_bucket_;
        if (!LookupBucketFor(Val, the_bucket_)) return false;  // not in map.

        the_bucket_->GetSecond().~ValueT();
        the_bucket_->GetFirst() = GetTombstoneKey();
        decrement_num_entries();
        incrementnum_to_mbstones_();
        return true;
    }
    void erase(iterator I) {
        BucketT *the_bucket_ = &*I;
        the_bucket_->GetSecond().~ValueT();
        the_bucket_->GetFirst() = GetTombstoneKey();
        decrement_num_entries();
        incrementnum_to_mbstones_();
    }

    value_type &FindAndConstruct(const KeyT &Key) {
        BucketT *the_bucket_;
        if (LookupBucketFor(Key, the_bucket_)) return *the_bucket_;

        return *InsertIntoBucket(the_bucket_, Key);
    }

    ValueT &operator[](const KeyT &Key) { return FindAndConstruct(Key).second; }

    value_type &FindAndConstruct(KeyT &&Key) {
        BucketT *the_bucket_;
        if (LookupBucketFor(Key, the_bucket_)) return *the_bucket_;

        return *InsertIntoBucket(the_bucket_, std::move(Key));
    }

    ValueT &operator[](KeyT &&Key) {
        return FindAndConstruct(std::move(Key)).second;
    }

    bool isPointerIntoBucketsArray(const void *ptr) const {
        return ptr >= getBuckets() && ptr < getBucketsend();
    }

    /// getPointerIntoBucketsArray() - Return an opaque pointer into the buckets
    /// array.  In conjunction with the previous method, this can be used to
    /// determine whether an insertion caused the HashMap to reAllocate.
    const void *getPointerIntoBucketsArray() const { return getBuckets(); }

protected:
    HashMapBase() = default;

    void DestroyAll() {
        if (getNumBukets() == 0)  // Nothing to do.
            return;

        const KeyT EmptyKey = GetEmptyKey(), TombstoneKey = GetTombstoneKey();
        for (BucketT *P = getBuckets(), *E = getBucketsend(); P != E; ++P) {
            if (!KeyInfoT::IsEqual(P->GetFirst(), EmptyKey) &&
                !KeyInfoT::IsEqual(P->GetFirst(), TombstoneKey))
                P->GetSecond().~ValueT();
            P->GetFirst().~KeyT();
        }
    }

    void initEmpty() {
        set_num_entries(0);
        set_num_to_mbstones(0);

        assert((getNumBukets() & (getNumBukets() - 1)) == 0 &&
               "# initial buckets must be a power of two!");
        const KeyT EmptyKey = GetEmptyKey();
        for (BucketT *B = getBuckets(), *E = getBucketsend(); B != E; ++B)
            ::new (&B->GetFirst()) KeyT(EmptyKey);
    }

    /// Returns the number of buckets to Allocate to ensure that the HashMap can
    /// accommodate \p num_entries_ without need to Grow().
    unsigned getMinBucketToReserveForEntries(unsigned num_entries_) {
        // Ensure that "num_entries_ * 4 < num_buckets_ * 3"
        if (num_entries_ == 0) return 0;
        // +1 is required because of the strict equality.
        // For example if num_entries_ is 48, we need to return 401.
        return NextPowerOf2(num_entries_ * 4 / 3 + 1);
    }

    void moveFromOldBuckets(BucketT *OldBucketsBegin, BucketT *OldBucketsend) {
        initEmpty();

        // Insert all the old elements.
        const KeyT EmptyKey = GetEmptyKey();
        const KeyT TombstoneKey = GetTombstoneKey();
        for (BucketT *B = OldBucketsBegin, *E = OldBucketsend; B != E; ++B) {
            if (!KeyInfoT::IsEqual(B->GetFirst(), EmptyKey) &&
                !KeyInfoT::IsEqual(B->GetFirst(), TombstoneKey)) {
                // Insert the key/value into the new table.
                BucketT *DestBucket;
                bool FoundVal = LookupBucketFor(B->GetFirst(), DestBucket);
                (void)FoundVal;  // silence warning.
                assert(!FoundVal && "Key already in new map?");
                DestBucket->GetFirst() = std::move(B->GetFirst());
                ::new (&DestBucket->GetSecond())
                    ValueT(std::move(B->GetSecond()));
                incrementnum_entries_();

                // Free the value.
                B->GetSecond().~ValueT();
            }
            B->GetFirst().~KeyT();
        }
    }

    template <typename OtherBaseT>
    void CopyFrom(
        const HashMapBase<OtherBaseT, KeyT, ValueT, KeyInfoT, BucketT> &other) {
        assert(&other != this);
        assert(getNumBukets() == other.getNumBukets());

        set_num_entries(other.num_entries());
        set_num_to_mbstones(other.num_to_mbstones());

        if (std::is_pod<KeyT>::value && std::is_pod<ValueT>::value)
            std::memcpy(reinterpret_cast<void *>(getBuckets()),
                        other.getBuckets(), getNumBukets() * sizeof(BucketT));
        else
            for (size_t i = 0; i < getNumBukets(); ++i) {
                ::new (&getBuckets()[i].GetFirst())
                    KeyT(other.getBuckets()[i].GetFirst());
                if (!KeyInfoT::IsEqual(getBuckets()[i].GetFirst(),
                                       GetEmptyKey()) &&
                    !KeyInfoT::IsEqual(getBuckets()[i].GetFirst(),
                                       GetTombstoneKey()))
                    ::new (&getBuckets()[i].GetSecond())
                        ValueT(other.getBuckets()[i].GetSecond());
            }
    }

    static unsigned GetHashValue(const KeyT &Val) {
        return KeyInfoT::GetHashValue(Val);
    }

    template <typename LookupKeyT>
    static unsigned GetHashValue(const LookupKeyT &Val) {
        return KeyInfoT::GetHashValue(Val);
    }

    static const KeyT GetEmptyKey() {
        static_assert(std::is_base_of<HashMapBase, DerivedT>::value,
                      "Must pass the derived type to this template!");
        return KeyInfoT::GetEmptyKey();
    }

    static const KeyT GetTombstoneKey() { return KeyInfoT::GetTombstoneKey(); }

private:
    iterator MakeIterator(BucketT *P, BucketT *E, bool NoAdvance = false) {
        return iterator(P, E, NoAdvance);
    }

    const_iterator MakeConstIterator(const BucketT *P, const BucketT *E,
                                     const bool NoAdvance = false) const {
        //    if (shouldReverseIterate<KeyT>()) {
        //      const BucketT *B = P == getBucketsend() ? getBuckets() : P + 1;
        //      return const_iterator(B, E, NoAdvance);
        //    }
        return const_iterator(P, E, NoAdvance);
    }

    unsigned num_entries() const {
        return static_cast<const DerivedT *>(this)->num_entries();
    }

    void set_num_entries(unsigned Num) {
        static_cast<DerivedT *>(this)->set_num_entries(Num);
    }

    void incrementnum_entries_() { set_num_entries(num_entries() + 1); }

    void decrement_num_entries() { set_num_entries(num_entries() - 1); }

    unsigned num_to_mbstones() const {
        return static_cast<const DerivedT *>(this)->num_to_mbstones();
    }

    void set_num_to_mbstones(unsigned Num) {
        static_cast<DerivedT *>(this)->set_num_to_mbstones(Num);
    }

    void incrementnum_to_mbstones_() {
        set_num_to_mbstones(num_to_mbstones() + 1);
    }

    void decrementnum_to_mbstones_() {
        set_num_to_mbstones(num_to_mbstones() - 1);
    }

    const BucketT *getBuckets() const {
        return static_cast<const DerivedT *>(this)->getBuckets();
    }

    BucketT *getBuckets() {
        return static_cast<DerivedT *>(this)->getBuckets();
    }

    unsigned getNumBukets() const {
        return static_cast<const DerivedT *>(this)->getNumBukets();
    }

    BucketT *getBucketsend() { return getBuckets() + getNumBukets(); }

    const BucketT *getBucketsend() const {
        return getBuckets() + getNumBukets();
    }

    void Grow(unsigned AtLeast) {
        static_cast<DerivedT *>(this)->Grow(AtLeast);
    }

    void shrink_and_clear() {
        static_cast<DerivedT *>(this)->shrink_and_clear();
    }

    template <typename KeyArg, typename... ValueArgs>
    BucketT *InsertIntoBucket(BucketT *the_bucket_, KeyArg &&Key,
                              ValueArgs &&... Values) {
        the_bucket_ = InsertIntoBucketImpl(Key, Key, the_bucket_);

        the_bucket_->GetFirst() = std::forward<KeyArg>(Key);
        ::new (&the_bucket_->GetSecond())
            ValueT(std::forward<ValueArgs>(Values)...);
        return the_bucket_;
    }

    template <typename LookupKeyT>
    BucketT *InsertIntoBucketWithLookup(BucketT *the_bucket_, KeyT &&Key,
                                        ValueT &&Value, LookupKeyT &Lookup) {
        the_bucket_ = InsertIntoBucketImpl(Key, Lookup, the_bucket_);

        the_bucket_->GetFirst() = std::move(Key);
        ::new (&the_bucket_->GetSecond()) ValueT(std::move(Value));
        return the_bucket_;
    }

    template <typename LookupKeyT>
    BucketT *InsertIntoBucketImpl(const KeyT &Key, const LookupKeyT &Lookup,
                                  BucketT *the_bucket_) {
        unsigned Newnum_entries_ = num_entries() + 1;
        unsigned num_buckets_ = getNumBukets();
        if (Newnum_entries_ * 4 >= num_buckets_ * 3) {
            this->Grow(num_buckets_ * 2);
            LookupBucketFor(Lookup, the_bucket_);
            num_buckets_ = getNumBukets();
        } else if (num_buckets_ - (Newnum_entries_ + num_to_mbstones()) <=
                   num_buckets_ / 8) {
            this->Grow(num_buckets_);
            LookupBucketFor(Lookup, the_bucket_);
        }
        assert(the_bucket_);

        // Only update the state after we've Grown our bucket space
        // appropriately so that when Growing buckets we have self-consistent
        // entry count.
        incrementnum_entries_();

        // If we are writing over a tombstone, remember this.
        const KeyT EmptyKey = GetEmptyKey();
        if (!KeyInfoT::IsEqual(the_bucket_->GetFirst(), EmptyKey))
            decrementnum_to_mbstones_();

        return the_bucket_;
    }

    /// LookupBucketFor - Lookup the appropriate bucket for Val, returning it in
    /// FoundBucket.  If the bucket contains the key and a value, this returns
    /// true, otherwise it returns a bucket with an empty marker or tombstone
    /// and returns false.
    template <typename LookupKeyT>
    bool LookupBucketFor(const LookupKeyT &Val,
                         const BucketT *&FoundBucket) const {
        const BucketT *Bucketsptr = getBuckets();
        const unsigned num_buckets_ = getNumBukets();

        if (num_buckets_ == 0) {
            FoundBucket = nullptr;
            return false;
        }

        // FoundTombstone - Keep track of whether we find a tombstone while
        // probing.
        const BucketT *FoundTombstone = nullptr;
        const KeyT EmptyKey = GetEmptyKey();
        const KeyT TombstoneKey = GetTombstoneKey();
        assert(!KeyInfoT::IsEqual(Val, EmptyKey) &&
               !KeyInfoT::IsEqual(Val, TombstoneKey) &&
               "Empty/Tombstone value shouldn't be inserted into map!");

        unsigned BucketNo = GetHashValue(Val) & (num_buckets_ - 1);
        unsigned ProbeAmt = 1;
        while (true) {
            const BucketT *ThisBucket = Bucketsptr + BucketNo;
            // Found Val's bucket?  If so, return it.
            if (KeyInfoT::IsEqual(Val, ThisBucket->GetFirst())) {
                FoundBucket = ThisBucket;
                return true;
            }

            // If we found an empty bucket, the key doesn't exist in the set.
            // Insert it and return the default value.
            if (KeyInfoT::IsEqual(ThisBucket->GetFirst(), EmptyKey)) {
                // If we've already seen a tombstone while probing, fill it in
                // instead of the empty bucket we eventually probed to.
                FoundBucket = FoundTombstone ? FoundTombstone : ThisBucket;
                return false;
            }

            // If this is a tombstone, remember it.  If Val ends up not in the
            // map, we prefer to return it than something that would require
            // more probing.
            if (KeyInfoT::IsEqual(ThisBucket->GetFirst(), TombstoneKey) &&
                !FoundTombstone)
                FoundTombstone =
                    ThisBucket;  // Remember the first tombstone found.

            // Otherwise, it's a hash collision or a tombstone, continue
            // quadratic probing.
            BucketNo += ProbeAmt++;
            BucketNo &= (num_buckets_ - 1);
        }
    }

    template <typename LookupKeyT>
    bool LookupBucketFor(const LookupKeyT &Val, BucketT *&FoundBucket) {
        const BucketT *ConstFoundBucket;
        bool Result = const_cast<const HashMapBase *>(this)->LookupBucketFor(
            Val, ConstFoundBucket);
        FoundBucket = const_cast<BucketT *>(ConstFoundBucket);
        return Result;
    }

public:
    /// Return the approximate size (in bytes) of the actual map.
    /// This is just the raw memory used by HashMap.
    /// If entries are pointers to objects, the size of the referenced objects
    /// are not included.
    size_t getMemorySize() const { return getNumBukets() * sizeof(BucketT); }
};

template <typename KeyT, typename ValueT, typename KeyInfoT = HashMapInfo<KeyT>,
          typename BucketT = detail::HashMapPair<KeyT, ValueT>>
class HashMap : public HashMapBase<HashMap<KeyT, ValueT, KeyInfoT, BucketT>,
                                   KeyT, ValueT, KeyInfoT, BucketT> {
    friend class HashMapBase<HashMap, KeyT, ValueT, KeyInfoT, BucketT>;

    // Lift some types from the dependent base class into this class for
    // simplicity of referring to them.
    using BaseT = HashMapBase<HashMap, KeyT, ValueT, KeyInfoT, BucketT>;

    BucketT *Buckets;
    unsigned num_entries_;
    unsigned num_to_mbstones_;
    unsigned num_buckets_;

public:
    /// Create a HashMap wth an optional \p InitialReserve that guarantee that
    /// this number of elements can be inserted in the map without Grow()
    explicit HashMap(unsigned InitialReserve = 0) { init(InitialReserve); }

    HashMap(const HashMap &other) : BaseT() {
        init(0);
        CopyFrom(other);
    }

    HashMap(HashMap &&other) : BaseT() {
        init(0);
        swap(other);
    }

    template <typename InputIt>
    HashMap(const InputIt &I, const InputIt &E) {
        init(std::distance(I, E));
        this->insert(I, E);
    }

    ~HashMap() {
        this->DestroyAll();
        operator delete(Buckets);
    }

    void swap(HashMap &RHS) {
        std::swap(Buckets, RHS.Buckets);
        std::swap(num_entries_, RHS.num_entries_);
        std::swap(num_to_mbstones_, RHS.num_to_mbstones_);
        std::swap(num_buckets_, RHS.num_buckets_);
    }

    HashMap &operator=(const HashMap &other) {
        if (&other != this) CopyFrom(other);
        return *this;
    }

    HashMap &operator=(HashMap &&other) {
        this->DestroyAll();
        operator delete(Buckets);
        init(0);
        swap(other);
        return *this;
    }

    void CopyFrom(const HashMap &other) {
        this->DestroyAll();
        operator delete(Buckets);
        if (AllocateBuckets(other.num_buckets_)) {
            this->BaseT::CopyFrom(other);
        } else {
            num_entries_ = 0;
            num_to_mbstones_ = 0;
        }
    }

    void init(unsigned Initnum_entries_) {
        auto InitBuckets =
            BaseT::getMinBucketToReserveForEntries(Initnum_entries_);
        if (AllocateBuckets(InitBuckets)) {
            this->BaseT::initEmpty();
        } else {
            num_entries_ = 0;
            num_to_mbstones_ = 0;
        }
    }

    void Grow(unsigned AtLeast) {
        unsigned Oldnum_buckets_ = num_buckets_;
        BucketT *OldBuckets = Buckets;

        AllocateBuckets(std::max<unsigned>(
            64, static_cast<unsigned>(NextPowerOf2(AtLeast - 1))));
        assert(Buckets);
        if (!OldBuckets) {
            this->BaseT::initEmpty();
            return;
        }

        this->moveFromOldBuckets(OldBuckets, OldBuckets + Oldnum_buckets_);

        // Free the old table.
        operator delete(OldBuckets);
    }

    void shrink_and_clear() {
        unsigned Oldnum_entries_ = num_entries_;
        this->DestroyAll();

        // Reduce the number of buckets.
        unsigned Newnum_buckets_ = 0;
        if (Oldnum_entries_)
            Newnum_buckets_ =
                std::max(64, 1 << (Log2_32_Ceil(Oldnum_entries_) + 1));
        if (Newnum_buckets_ == num_buckets_) {
            this->BaseT::initEmpty();
            return;
        }

        operator delete(Buckets);
        init(Newnum_buckets_);
    }

private:
    unsigned num_entries() const { return num_entries_; }

    void set_num_entries(unsigned Num) { num_entries_ = Num; }

    unsigned num_to_mbstones() const { return num_to_mbstones_; }

    void set_num_to_mbstones(unsigned Num) { num_to_mbstones_ = Num; }

    BucketT *getBuckets() const { return Buckets; }

    unsigned getNumBukets() const { return num_buckets_; }

    bool AllocateBuckets(unsigned Num) {
        num_buckets_ = Num;
        if (num_buckets_ == 0) {
            Buckets = nullptr;
            return false;
        }

        Buckets = static_cast<BucketT *>(operator new(sizeof(BucketT) *
                                                      num_buckets_));
        return true;
    }
};

template <typename KeyT, typename ValueT, unsigned InlineBuckets = 4,
          typename KeyInfoT = HashMapInfo<KeyT>,
          typename BucketT = detail::HashMapPair<KeyT, ValueT>>
class SmallHashMap
    : public HashMapBase<
          SmallHashMap<KeyT, ValueT, InlineBuckets, KeyInfoT, BucketT>, KeyT,
          ValueT, KeyInfoT, BucketT> {
    friend class HashMapBase<SmallHashMap, KeyT, ValueT, KeyInfoT, BucketT>;

    // Lift some types from the dependent base class into this class for
    // simplicity of referring to them.
    using BaseT = HashMapBase<SmallHashMap, KeyT, ValueT, KeyInfoT, BucketT>;

    static_assert(isPowerOf2_64(InlineBuckets),
                  "InlineBuckets must be a power of 2.");

    unsigned Small : 1;
    unsigned num_entries_ : 31;
    unsigned num_to_mbstones_;

    struct LargeRep {
        BucketT *Buckets;
        unsigned num_buckets_;
    };

    AlignedCharArrayUnion<BucketT[InlineBuckets], LargeRep> storage;

public:
    explicit SmallHashMap(unsigned NumInitBuckets = 0) { init(NumInitBuckets); }

    SmallHashMap(const SmallHashMap &other) : BaseT() {
        init(0);
        CopyFrom(other);
    }

    SmallHashMap(SmallHashMap &&other) : BaseT() {
        init(0);
        swap(other);
    }

    template <typename InputIt>
    SmallHashMap(const InputIt &I, const InputIt &E) {
        init(NextPowerOf2(std::distance(I, E)));
        this->insert(I, E);
    }

    ~SmallHashMap() {
        this->DestroyAll();
        DeallocateBuckets();
    }

    void swap(SmallHashMap &RHS) {
        unsigned Tmpnum_entries_ = RHS.num_entries_;
        RHS.num_entries_ = num_entries_;
        num_entries_ = Tmpnum_entries_;
        std::swap(num_to_mbstones_, RHS.num_to_mbstones_);

        const KeyT EmptyKey = this->GetEmptyKey();
        const KeyT TombstoneKey = this->GetTombstoneKey();
        if (Small && RHS.Small) {
            for (unsigned i = 0, e = InlineBuckets; i != e; ++i) {
                BucketT *LHSB = &getInlineBuckets()[i],
                        *RHSB = &RHS.getInlineBuckets()[i];
                bool hasLHSValue =
                    (!KeyInfoT::IsEqual(LHSB->GetFirst(), EmptyKey) &&
                     !KeyInfoT::IsEqual(LHSB->GetFirst(), TombstoneKey));
                bool hasRHSValue =
                    (!KeyInfoT::IsEqual(RHSB->GetFirst(), EmptyKey) &&
                     !KeyInfoT::IsEqual(RHSB->GetFirst(), TombstoneKey));
                if (hasLHSValue && hasRHSValue) {
                    // Swap together if we can...
                    std::swap(*LHSB, *RHSB);
                    continue;
                }
                // Swap separately and handle any assymetry.
                std::swap(LHSB->GetFirst(), RHSB->GetFirst());
                if (hasLHSValue) {
                    ::new (&RHSB->GetSecond())
                        ValueT(std::move(LHSB->GetSecond()));
                    LHSB->GetSecond().~ValueT();
                } else if (hasRHSValue) {
                    ::new (&LHSB->GetSecond())
                        ValueT(std::move(RHSB->GetSecond()));
                    RHSB->GetSecond().~ValueT();
                }
            }
            return;
        }
        if (!Small && !RHS.Small) {
            std::swap(getLargeRep()->Buckets, RHS.getLargeRep()->Buckets);
            std::swap(getLargeRep()->num_buckets_,
                      RHS.getLargeRep()->num_buckets_);
            return;
        }

        SmallHashMap &SmallSide = Small ? *this : RHS;
        SmallHashMap &LargeSide = Small ? RHS : *this;

        // First stash the large side's rep and move the small side across.
        LargeRep TmpRep = std::move(*LargeSide.getLargeRep());
        LargeSide.getLargeRep()->~LargeRep();
        LargeSide.Small = true;
        // This is similar to the standard move-from-old-buckets, but the bucket
        // count hasn't actually rotated in this case. So we have to carefully
        // move construct the keys and values into their new locations, but
        // there is no need to re-hash things.
        for (unsigned i = 0, e = InlineBuckets; i != e; ++i) {
            BucketT *NewB = &LargeSide.getInlineBuckets()[i],
                    *OldB = &SmallSide.getInlineBuckets()[i];
            ::new (&NewB->GetFirst()) KeyT(std::move(OldB->GetFirst()));
            OldB->GetFirst().~KeyT();
            if (!KeyInfoT::IsEqual(NewB->GetFirst(), EmptyKey) &&
                !KeyInfoT::IsEqual(NewB->GetFirst(), TombstoneKey)) {
                ::new (&NewB->GetSecond()) ValueT(std::move(OldB->GetSecond()));
                OldB->GetSecond().~ValueT();
            }
        }

        // The hard part of moving the small buckets across is done, just move
        // the TmpRep into its new home.
        SmallSide.Small = false;
        new (SmallSide.getLargeRep()) LargeRep(std::move(TmpRep));
    }

    SmallHashMap &operator=(const SmallHashMap &other) {
        if (&other != this) CopyFrom(other);
        return *this;
    }

    SmallHashMap &operator=(SmallHashMap &&other) {
        this->DestroyAll();
        DeallocateBuckets();
        init(0);
        swap(other);
        return *this;
    }

    void CopyFrom(const SmallHashMap &other) {
        this->DestroyAll();
        DeallocateBuckets();
        Small = true;
        if (other.getNumBukets() > InlineBuckets) {
            Small = false;
            new (getLargeRep()) LargeRep(AllocateBuckets(other.getNumBukets()));
        }
        this->BaseT::CopyFrom(other);
    }

    void init(unsigned InitBuckets) {
        Small = true;
        if (InitBuckets > InlineBuckets) {
            Small = false;
            new (getLargeRep()) LargeRep(AllocateBuckets(InitBuckets));
        }
        this->BaseT::initEmpty();
    }

    void Grow(unsigned AtLeast) {
        if (AtLeast >= InlineBuckets)
            AtLeast = std::max<unsigned>(64, NextPowerOf2(AtLeast - 1));

        if (Small) {
            if (AtLeast < InlineBuckets) return;  // Nothing to do.

            // First move the inline buckets into a temporary storage.
            AlignedCharArrayUnion<BucketT[InlineBuckets]> TmpStorage;
            BucketT *TmpBegin = reinterpret_cast<BucketT *>(TmpStorage.buffer);
            BucketT *Tmpend = TmpBegin;

            // Loop over the buckets, moving non-empty, non-tombstones into the
            // temporary storage. Have the loop move the Tmpend forward as it
            // goes.
            const KeyT EmptyKey = this->GetEmptyKey();
            const KeyT TombstoneKey = this->GetTombstoneKey();
            for (BucketT *P = getBuckets(), *E = P + InlineBuckets; P != E;
                 ++P) {
                if (!KeyInfoT::IsEqual(P->GetFirst(), EmptyKey) &&
                    !KeyInfoT::IsEqual(P->GetFirst(), TombstoneKey)) {
                    assert(size_t(Tmpend - TmpBegin) < InlineBuckets &&
                           "Too many inline buckets!");
                    ::new (&Tmpend->GetFirst()) KeyT(std::move(P->GetFirst()));
                    ::new (&Tmpend->GetSecond())
                        ValueT(std::move(P->GetSecond()));
                    ++Tmpend;
                    P->GetSecond().~ValueT();
                }
                P->GetFirst().~KeyT();
            }

            // Now Make this map use the large rep, and move all the entries
            // back into it.
            Small = false;
            new (getLargeRep()) LargeRep(AllocateBuckets(AtLeast));
            this->moveFromOldBuckets(TmpBegin, Tmpend);
            return;
        }

        LargeRep OldRep = std::move(*getLargeRep());
        getLargeRep()->~LargeRep();
        if (AtLeast <= InlineBuckets) {
            Small = true;
        } else {
            new (getLargeRep()) LargeRep(AllocateBuckets(AtLeast));
        }

        this->moveFromOldBuckets(OldRep.Buckets,
                                 OldRep.Buckets + OldRep.num_buckets_);

        // Free the old table.
        operator delete(OldRep.Buckets);
    }

    void shrink_and_clear() {
        unsigned OldSize = this->size();
        this->DestroyAll();

        // Reduce the number of buckets.
        unsigned Newnum_buckets_ = 0;
        if (OldSize) {
            Newnum_buckets_ = 1 << (Log2_32_Ceil(OldSize) + 1);
            if (Newnum_buckets_ > InlineBuckets && Newnum_buckets_ < 64u)
                Newnum_buckets_ = 64;
        }
        if ((Small && Newnum_buckets_ <= InlineBuckets) ||
            (!Small && Newnum_buckets_ == getLargeRep()->num_buckets_)) {
            this->BaseT::initEmpty();
            return;
        }

        DeallocateBuckets();
        init(Newnum_buckets_);
    }

private:
    unsigned num_entries() const { return num_entries_; }

    void set_num_entries(unsigned Num) {
        // num_entries_ is hardcoded to be 31 bits wide.
        assert(Num < (1U << 31) && "Cannot support more than 1<<31 entries");
        num_entries_ = Num;
    }

    unsigned num_to_mbstones() const { return num_to_mbstones_; }

    void set_num_to_mbstones(unsigned Num) { num_to_mbstones_ = Num; }

    const BucketT *getInlineBuckets() const {
        assert(Small);
        return reinterpret_cast<const BucketT *>(storage.buffer);
    }

    BucketT *getInlineBuckets() {
        return const_cast<BucketT *>(
            const_cast<const SmallHashMap *>(this)->getInlineBuckets());
    }

    const LargeRep *getLargeRep() const {
        assert(!Small);
        return reinterpret_cast<const LargeRep *>(storage.buffer);
    }

    LargeRep *getLargeRep() {
        return const_cast<LargeRep *>(
            const_cast<const SmallHashMap *>(this)->getLargeRep());
    }

    const BucketT *getBuckets() const {
        return Small ? getInlineBuckets() : getLargeRep()->Buckets;
    }

    BucketT *getBuckets() {
        return const_cast<BucketT *>(
            const_cast<const SmallHashMap *>(this)->getBuckets());
    }

    unsigned getNumBukets() const {
        return Small ? InlineBuckets : getLargeRep()->num_buckets_;
    }

    void DeallocateBuckets() {
        if (Small) return;

        operator delete(getLargeRep()->Buckets);
        getLargeRep()->~LargeRep();
    }

    LargeRep AllocateBuckets(unsigned Num) {
        assert(Num > InlineBuckets &&
               "Must Allocate more buckets than are inline");
        LargeRep Rep = {
            static_cast<BucketT *>(operator new(sizeof(BucketT) * Num)), Num};
        return Rep;
    }
};

template <typename KeyT, typename ValueT, typename KeyInfoT, typename Bucket,
          bool IsConst>
class HashMapIterator {
    friend class HashMapIterator<KeyT, ValueT, KeyInfoT, Bucket, true>;
    friend class HashMapIterator<KeyT, ValueT, KeyInfoT, Bucket, false>;

    using ConstIterator = HashMapIterator<KeyT, ValueT, KeyInfoT, Bucket, true>;

public:
    using difference_type = ptrdiff_t;
    using value_type =
        typename std::conditional<IsConst, const Bucket, Bucket>::type;
    using pointer = value_type *;
    using reference = value_type &;
    using iterator_category = std::forward_iterator_tag;

private:
    pointer ptr = nullptr;
    pointer end = nullptr;

public:
    HashMapIterator() = default;

    HashMapIterator(pointer Pos, pointer E, bool NoAdvance = false)
        : ptr(Pos), end(E) {
        if (NoAdvance) return;
        AdvancePastEmptyBuckets();
    }

    template <bool IsConstSrc,
              typename = typename std::enable_if<!IsConstSrc && IsConst>::type>
    HashMapIterator(
        const HashMapIterator<KeyT, ValueT, KeyInfoT, Bucket, IsConstSrc> &I)
        : ptr(I.ptr), end(I.end) {}

    reference operator*() const { return *ptr; }
    pointer operator->() const { return ptr; }

    bool operator==(const ConstIterator &RHS) const { return ptr == RHS.ptr; }
    bool operator!=(const ConstIterator &RHS) const { return ptr != RHS.ptr; }

    inline HashMapIterator &operator++() {  // Preincrement
        ++ptr;
        AdvancePastEmptyBuckets();
        return *this;
    }
    HashMapIterator operator++(int) {  // Postincrement
        HashMapIterator tmp = *this;
        ++*this;
        return tmp;
    }

private:
    void AdvancePastEmptyBuckets() {
        assert(ptr <= end);
        const KeyT Empty = KeyInfoT::GetEmptyKey();
        const KeyT Tombstone = KeyInfoT::GetTombstoneKey();

        while (ptr != end && (KeyInfoT::IsEqual(ptr->GetFirst(), Empty) ||
                              KeyInfoT::IsEqual(ptr->GetFirst(), Tombstone)))
            ++ptr;
    }

    void RetreatPastEmptyBuckets() {
        assert(ptr >= end);
        const KeyT Empty = KeyInfoT::GetEmptyKey();
        const KeyT Tombstone = KeyInfoT::GetTombstoneKey();

        while (ptr != end && (KeyInfoT::IsEqual(ptr[-1].GetFirst(), Empty) ||
                              KeyInfoT::IsEqual(ptr[-1].GetFirst(), Tombstone)))
            --ptr;
    }
};
