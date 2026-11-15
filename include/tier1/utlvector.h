//====== Copyright � 1996-2005, Valve Corporation, All rights reserved. =======//
//
// Purpose: 
//
// $NoKeywords: $
//
// A growable array class that maintains a free list and keeps elements
// in the same location
//=============================================================================//

#pragma once


#include <string.h>
#include "tier0/platform.h"

#include "tier1/utlmemory.h"
#include "tier1/utlblockmemory.h"
#include "tier1/strtools.h"

#include <algorithm>
#include <type_traits>


//-----------------------------------------------------------------------------
// The CUtlVector class:
// A growable array class which doubles in size by default.
// It will always keep all elements consecutive in memory, and may move the
// elements around in memory (via a PvRealloc) when elements are inserted or
// removed. Clients should therefore refer to the elements of the vector
// by index (they should *never* maintain pointers to elements in the vector).
//-----------------------------------------------------------------------------
template<class T, class A = CUtlMemory<T> >
class CUtlVector {
    using CAllocator = A;

public:
    using ElemType_t = T;
    using iterator = T*;
    using const_iterator = const T*;

    // Set the growth policy and initial capacity. Count will always be zero. This is different from std::vector
    // where the constructor sets count as well as capacity.
    // growSize of zero implies the default growth pattern which is exponential.
    explicit CUtlVector(uint32_t growSize = 0, uint32_t initialCapacity = 0);

    // Initialize with separately allocated buffer, setting the capacity and count.
    // The container will not be growable.
    CUtlVector(T* pMemory, uint32_t initialCapacity, uint32_t initialCount = 0);

    ~CUtlVector();

    // Copy the array.
    CUtlVector<T, A>& operator=(const CUtlVector<T, A> &other);

    // element access
    T& operator[](size_t i);

    const T& operator[](size_t i) const;

    T& at(size_t i);

    const T& at(size_t i) const;

    T& front();

    const T& front() const;

    T& back();

    const T& back() const;

    // STL compatible member functions. These allow easier use of std::sort
    // and they are forward compatible with the C++ 11 range-based for loops.
    iterator begin() { return data(); }
    const_iterator begin() const { return data(); }
    iterator end() { return data() + size(); }
    const_iterator end() const { return data() + size(); }

    // Gets the base address (can change when adding elements!)
    T* data() { return m_Memory.data(); }
    const T* data() const { return m_Memory.data(); }

    // Returns the number of elements in the vector
    int size() const;

    /// are there no elements? For compatibility with lists.
    bool empty() const {
        return (size() == 0);
    }

    // Is element index valid?
    bool IsValidIndex(int i) const;

    static int InvalidIndex();

    // Adds an element, uses default constructor
    int AddToHead();

    int AddToTail();

    T* AddToTailGetPtr();

    int InsertBefore(int elem);

    int InsertAfter(int elem);

    // Adds an element, uses copy constructor
    int AddToHead(const T &src);

    int AddToTail(const T &src);

    int InsertBefore(int elem, const T &src);

    int InsertAfter(int elem, const T &src);

    // Adds multiple elements, uses default constructor
    int AddMultipleToHead(int num);

    int AddMultipleToTail(int num);

    int AddMultipleToTail(int num, const T* pToCopy);

    int InsertMultipleBefore(int elem, int num);

    int InsertMultipleBefore(int elem, int num, const T* pToCopy);

    int InsertMultipleAfter(int elem, int num);

    // Calls RemoveAll() then AddMultipleToTail.
    // SetSize is a synonym for SetCount
    void SetSize(int size);

    // SetCount deletes the previous contents of the container and sets the
    // container to have this many elements.
    // Use GetCount to retrieve the current count.
    void SetCount(int count);

    void SetCountNonDestructively(int count);

    //sets count by adding or removing elements to tail TODO: This should probably be the default behavior for SetCount

    // Calls SetSize and copies each element.
    void CopyArray(const T* pArray, int size);

    // Fast swap
    void Swap(CUtlVector<T, A> &vec);

    // Add the specified array to the tail.
    int AddVectorToTail(CUtlVector<T, A> const &src);

    // Finds an element (element needs operator== defined)
    int Find(const T &src) const;

    void FillWithValue(const T &src);

    bool HasElement(const T &src) const;

    // Makes sure we have enough memory allocated to store a requested # of elements
    // Use NumAllocated() to retrieve the current capacity.
    void EnsureCapacity(uint32_t num);

    // Makes sure we have at least this many elements
    // Use GetCount to retrieve the current count.
    void EnsureCount(uint32_t num);

    // Element removal
    void FastRemove(int elem); // doesn't preserve order
    void Remove(int elem); // preserves order, shifts elements
    bool FindAndRemove(const T &src); // removes first occurrence of src, preserves order, shifts elements
    bool FindAndFastRemove(const T &src); // removes first occurrence of src, doesn't preserve order
    void RemoveMultiple(int elem, int num); // preserves order, shifts elements
    void RemoveMultipleFromHead(int num); // removes num elements from tail
    void RemoveMultipleFromTail(int num); // removes num elements from tail
    void RemoveAll(); // doesn't deallocate memory

    // Memory deallocation
    void Purge();

    // Purges the list and calls delete on each element in it.
    void PurgeAndDeleteElements();

    // Compacts the vector to the number of elements actually in use
    void Compact();

    // Set the size by which it grows when it needs to allocate more memory.
    void SetGrowSize(int size) { m_Memory.SetGrowSize(size); }

    int NumAllocated() const; // Only use this if you really know what you're doing!

    void Sort(int (__cdecl *pfnCompare)(const T*, const T*));

    // Call this to quickly sort non-contiguously allocated vectors
    void InPlaceQuickSort(int (__cdecl *pfnCompare)(const T*, const T*));

    // reverse the order of elements
    void Reverse();

#ifdef DBGFLAG_VALIDATE
    void Validate(CValidator &validator, char* pchName); // Validate our internal structures
#endif // DBGFLAG_VALIDATE


    int SortedFindLessOrEqual(const T &search, bool (__cdecl *pfnLessFunc)(const T &src1, const T &src2, void* pCtx),
                              void* pLessContext) const;

    int SortedInsert(const T &src, bool (__cdecl *pfnLessFunc)(const T &src1, const T &src2, void* pCtx),
                     void* pLessContext);

    /// sort using std:: and expecting a "<" function to be defined for the type
    void Sort();

    /// sort using std:: with a predicate. e.g. [] -> bool ( T &a, T &b ) { return a < b; }
    template<class F>
    void SortPredicate(F &&predicate);

protected:
    // Can't copy this unless we explicitly do it!
    CUtlVector(CUtlVector const &vec) { Assert(0); }

    // Grows the vector
    void GrowVector(int num = 1);


    // Shifts elements....
    void ShiftElementsRight(int elem, int num = 1);

    void ShiftElementsLeft(int elem, int num = 1);

    CAllocator m_Memory;
    int m_Size;

    // For easier access to the elements through the debugger
    // it's in release builds so this can be used in libraries correctly
    T* m_pElements;

    inline void ResetDbgInfo() {
        m_pElements = data();
    }

private:
    void InPlaceQuickSort_r(int (__cdecl *pfnCompare)(const T*, const T*), int nLeft, int nRight);
};


//-----------------------------------------------------------------------------
// The CUtlVectorMT class:
// An array class with spurious mutex protection. Nothing is actually protected
// unless you call Lock and Unlock. Also, the Mutex_t is actually not a type
// but a member which probably isn't used.
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// The CUtlVectorFixed class:
// A array class with a fixed allocation scheme
//-----------------------------------------------------------------------------
template<class T, size_t MAX_SIZE>
class CUtlVectorFixed : public CUtlVector<T, CUtlMemoryFixed<T, MAX_SIZE> > {
    typedef CUtlVector<T, CUtlMemoryFixed<T, MAX_SIZE> > BaseClass;

public:
    // constructor, destructor
    explicit CUtlVectorFixed(int growSize = 0, int initSize = 0) : BaseClass(growSize, initSize) {
    }

    CUtlVectorFixed(T* pMemory, int numElements) : BaseClass(pMemory, numElements) {
    }
};


//-----------------------------------------------------------------------------
// The CUtlVectorFixedGrowable class:
// A array class with a fixed allocation scheme backed by a dynamic one
//-----------------------------------------------------------------------------
template<class T, size_t MAX_SIZE>
class CUtlVectorFixedGrowable : public CUtlVector<T, CUtlMemoryFixedGrowable<T, MAX_SIZE> > {
    typedef CUtlVector<T, CUtlMemoryFixedGrowable<T, MAX_SIZE> > BaseClass;

public:
    // constructor, destructor
    explicit CUtlVectorFixedGrowable(int growSize = 0) : BaseClass(growSize, MAX_SIZE) {
    }
};


//-----------------------------------------------------------------------------
// The CUtlVectorUltra Conservative class:
// A array class with a very conservative allocation scheme, with customizable allocator
// Especialy useful if you have a lot of vectors that are sparse, or if you're
// carefully packing holders of vectors
//-----------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4200) // warning C4200: nonstandard extension used : zero-sized array in struct/union
#pragma warning(disable : 4815 ) // warning C4815: 'staticData' : zero-sized array in stack object will have no elements

#pragma warning(pop)

//-----------------------------------------------------------------------------
// constructor, destructor
//-----------------------------------------------------------------------------
template<typename T, class A>
inline CUtlVector<T, A>::CUtlVector(uint32_t growSize, uint32_t initialCapacity) : m_Memory(growSize, initialCapacity),
    m_Size(0) {
    ResetDbgInfo();
}

template<typename T, class A>
inline CUtlVector<T,
    A>::CUtlVector(T* pMemory, uint32_t initialCapacity, uint32_t initialCount) : m_Memory(pMemory, initialCapacity),
    m_Size(initialCount) {
    ResetDbgInfo();
}

template<typename T, class A>
inline CUtlVector<T, A>::~CUtlVector() {
    Purge();
}

template<typename T, class A>
inline CUtlVector<T, A>& CUtlVector<T, A>::operator=(const CUtlVector<T, A> &other) {
    int nCount = other.size();
    SetSize(nCount);
    for (int i = 0; i < nCount; i++) {
        (*this)[i] = other[i];
    }
    return *this;
}


//-----------------------------------------------------------------------------
// element access
//-----------------------------------------------------------------------------

template<typename T, class A>
inline T& CUtlVector<T, A>::operator[](const size_t i) {
    return m_Memory[i];
}

template<typename T, class A>
inline const T& CUtlVector<T, A>::operator[](const size_t i) const {
    return m_Memory[i];
}

template<typename T, class A>
inline T& CUtlVector<T, A>::at(size_t i) {
    return m_Memory[i];
}

template<typename T, class A>
inline const T& CUtlVector<T, A>::at(size_t i) const {
    return m_Memory[i];
}

template<typename T, class A>
inline T& CUtlVector<T, A>::front() {
    Assert(m_Size > 0);
    return m_Memory[0];
}

template<typename T, class A>
inline const T& CUtlVector<T, A>::front() const {
    Assert(m_Size > 0);
    return m_Memory[0];
}

template<typename T, class A>
inline T& CUtlVector<T, A>::back() {
    Assert(m_Size > 0);
    return m_Memory[m_Size - 1];
}

template<typename T, class A>
inline const T& CUtlVector<T, A>::back() const {
    Assert(m_Size > 0);
    return m_Memory[m_Size - 1];
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// Reverse - reverse the order of elements, akin to std::vector<>::reverse()
//-----------------------------------------------------------------------------
template<typename T, class A>
void CUtlVector<T, A>::Reverse() {
    for (int i = 0; i < m_Size / 2; i++) {
        V_swap(m_Memory[i], m_Memory[m_Size - 1 - i]);
#if defined( UTLVECTOR_TRACK_STACKS )
        if (bTrackingEnabled) {
            V_swap(m_pElementStackStatsIndices[i], m_pElementStackStatsIndices[m_Size - 1 - i]);
        }
#endif
    }
}

// Count
//-----------------------------------------------------------------------------
template<typename T, class A>
inline int CUtlVector<T, A>::size() const {
    return m_Size;
}


//-----------------------------------------------------------------------------
// Is element index valid?
//-----------------------------------------------------------------------------
template<typename T, class A>
inline bool CUtlVector<T, A>::IsValidIndex(int i) const {
    return (i >= 0) && (i < m_Size);
}


//-----------------------------------------------------------------------------
// Returns in invalid index
//-----------------------------------------------------------------------------
template<typename T, class A>
inline int CUtlVector<T, A>::InvalidIndex() {
    return -1;
}


//-----------------------------------------------------------------------------
// Grows the vector
//-----------------------------------------------------------------------------
template<typename T, class A>
void CUtlVector<T, A>::GrowVector(int num) {
    if (m_Size + num > m_Memory.NumAllocated()) {
        m_Memory.Grow(m_Size + num - m_Memory.NumAllocated());
    }

    m_Size += num;
    ResetDbgInfo();
}


//-----------------------------------------------------------------------------
// finds a particular element
// You must sort the list before using or your results will be wrong
//-----------------------------------------------------------------------------
template<typename T, class A>
int CUtlVector<T, A>::SortedFindLessOrEqual(const T &search,
                                            bool (__cdecl *pfnLessFunc)(const T &src1, const T &src2, void* pCtx),
                                            void* pLessContext) const {
    int start = 0, end = size() - 1;
    while (start <= end) {
        int mid = (start + end) >> 1;
        if (pfnLessFunc(at(mid), search, pLessContext)) {
            start = mid + 1;
        }
        else if (pfnLessFunc(search, at(mid), pLessContext)) {
            end = mid - 1;
        }
        else {
            return mid;
        }
    }
    return end;
}


template<typename T, class A>
int CUtlVector<T, A>::SortedInsert(const T &src, bool (__cdecl *pfnLessFunc)(const T &src1, const T &src2, void* pCtx),
                                   void* pLessContext) {
    int pos = SortedFindLessOrEqual(src, pfnLessFunc, pLessContext) + 1;
    GrowVector();
    ShiftElementsRight(pos);
    CopyConstruct<T>(&at(pos), src);
    //UTLVECTOR_STACK_STATS_ALLOCATED_SINGLE( pos );
    return pos;
}


//-----------------------------------------------------------------------------
// Sorts the vector
//-----------------------------------------------------------------------------
template<typename T, class A>
void CUtlVector<T, A>::Sort(int (__cdecl *pfnCompare)(const T*, const T*)) {
    typedef int (__cdecl *QSortCompareFunc_t)(const void*, const void*);
    if (size() <= 1)
        return;

    if (data()) {
        qsort(data(), size(), sizeof(T), (QSortCompareFunc_t) (pfnCompare));
    }
    else {
        Assert(0);
        // this path is untested
        // if you want to sort vectors that use a non-sequential memory allocator,
        // you'll probably want to patch in a quicksort algorithm here
        // I just threw in this bubble sort to have something just in case...

        for (int i = m_Size - 1; i >= 0; --i) {
            for (int j = 1; j <= i; ++j) {
                if (pfnCompare(&at(j - 1), &at(j)) < 0) {
                    V_swap(at(j - 1), at(j));
                }
            }
        }
    }
}


//----------------------------------------------------------------------------------------------
// Private function that does the in-place quicksort for non-contiguously allocated vectors.
//----------------------------------------------------------------------------------------------
template<typename T, class A>
void CUtlVector<T, A>::InPlaceQuickSort_r(int (__cdecl *pfnCompare)(const T*, const T*), int nLeft, int nRight) {
    int nPivot;
    int nLeftIdx = nLeft;
    int nRightIdx = nRight;

    if (nRight - nLeft > 0) {
        nPivot = (nLeft + nRight) / 2;

        while ((nLeftIdx <= nPivot) && (nRightIdx >= nPivot)) {
            while ((pfnCompare(&at(nLeftIdx), &at(nPivot)) < 0) && (nLeftIdx <= nPivot)) {
                nLeftIdx++;
            }

            while ((pfnCompare(&at(nRightIdx), &at(nPivot)) > 0) && (nRightIdx >= nPivot)) {
                nRightIdx--;
            }

            V_swap(at(nLeftIdx), at(nRightIdx));

            nLeftIdx++;
            nRightIdx--;

            if ((nLeftIdx - 1) == nPivot) {
                nPivot = nRightIdx = nRightIdx + 1;
            }
            else if (nRightIdx + 1 == nPivot) {
                nPivot = nLeftIdx = nLeftIdx - 1;
            }
        }

        InPlaceQuickSort_r(pfnCompare, nLeft, nPivot - 1);
        InPlaceQuickSort_r(pfnCompare, nPivot + 1, nRight);
    }
}


//----------------------------------------------------------------------------------------------
// Call this to quickly sort non-contiguously allocated vectors. Sort uses a slower bubble sort.
//----------------------------------------------------------------------------------------------
template<typename T, class A>
void CUtlVector<T, A>::InPlaceQuickSort(int (__cdecl *pfnCompare)(const T*, const T*)) {
    InPlaceQuickSort_r(pfnCompare, 0, size() - 1);
}

template<typename T, class A>
void CUtlVector<T, A>::Sort() {
    //STACK STATS TODO: Do we care about allocation tracking precision enough to match element origins across a sort?
    std::sort(data(), data() + size());
}

template<typename T, class A>
template<class F>
void CUtlVector<T, A>::SortPredicate(F &&predicate) {
    std::sort(data(), data() + size(), predicate);
}

//-----------------------------------------------------------------------------
// Makes sure we have enough memory allocated to store a requested # of elements
//-----------------------------------------------------------------------------
template<typename T, class A>
void CUtlVector<T, A>::EnsureCapacity(const uint32_t num) {
    m_Memory.EnsureCapacity(num);
    ResetDbgInfo();
}


//-----------------------------------------------------------------------------
// Makes sure we have at least this many elements
//-----------------------------------------------------------------------------
template<typename T, class A>
void CUtlVector<T, A>::EnsureCount(const uint32_t num) {
    if (size() < num) {
        AddMultipleToTail(num - size());
    }
}


//-----------------------------------------------------------------------------
// Shifts elements
//-----------------------------------------------------------------------------
template<typename T, class A>
void CUtlVector<T, A>::ShiftElementsRight(int elem, int num) {
    Assert(IsValidIndex(elem) || ( m_Size == 0 ) || ( num == 0 ));
    int numToMove = m_Size - elem - num;
    if ((numToMove > 0) && (num > 0))
        Q_memmove(&at(elem+num), &at(elem), numToMove * sizeof(T));
}

template<typename T, class A>
void CUtlVector<T, A>::ShiftElementsLeft(int elem, int num) {
    Assert(IsValidIndex(elem) || ( m_Size == 0 ) || ( num == 0 ));
    int numToMove = m_Size - elem - num;
    if ((numToMove > 0) && (num > 0)) {
        Q_memmove(&at(elem), &at(elem+num), numToMove * sizeof(T));

#ifdef _DEBUG
        Q_memset(&at(m_Size-num), 0xDD, num * sizeof(T));
#endif
    }
}


//-----------------------------------------------------------------------------
// Adds an element, uses default constructor
//-----------------------------------------------------------------------------
template<typename T, class A>
inline int CUtlVector<T, A>::AddToHead() {
    return InsertBefore(0);
}

template<typename T, class A>
inline int CUtlVector<T, A>::AddToTail() {
    return InsertBefore(m_Size);
}

template<typename T, class A>
inline T* CUtlVector<T, A>::AddToTailGetPtr() {
    return &at(AddToTail());
}

template<typename T, class A>
inline int CUtlVector<T, A>::InsertAfter(int elem) {
    return InsertBefore(elem + 1);
}

template<typename T, class A>
int CUtlVector<T, A>::InsertBefore(int elem) {
    // Can insert at the end
    Assert((elem == Count()) || IsValidIndex(elem));

    GrowVector();
    ShiftElementsRight(elem);
    Construct(&at(elem));
    return elem;
}


//-----------------------------------------------------------------------------
// Adds an element, uses copy constructor
//-----------------------------------------------------------------------------
template<typename T, class A>
inline int CUtlVector<T, A>::AddToHead(const T &src) {
    // Can't insert something that's in the list... reallocation may hose us
    Assert((Base() == NULL) || (&src < Base()) || (&src >= (Base() + Count()) ));
    return InsertBefore(0, src);
}

template<typename T, class A>
inline int CUtlVector<T, A>::AddToTail(const T &src) {
    // Can't insert something that's in the list... reallocation may hose us
    Assert((Base() == NULL) || (&src < Base()) || (&src >= (Base() + Count()) ));
    return InsertBefore(m_Size, src);
}

template<typename T, class A>
inline int CUtlVector<T, A>::InsertAfter(int elem, const T &src) {
    // Can't insert something that's in the list... reallocation may hose us
    Assert((Base() == NULL) || (&src < Base()) || (&src >= (Base() + Count()) ));
    return InsertBefore(elem + 1, src);
}

template<typename T, class A>
int CUtlVector<T, A>::InsertBefore(int elem, const T &src) {
    // Can't insert something that's in the list... reallocation may hose us
    Assert((Base() == NULL) || (&src < Base()) || (&src >= (Base() + Count()) ));

    // Can insert at the end
    Assert((elem == Count()) || IsValidIndex(elem));

    GrowVector();
    ShiftElementsRight(elem);
    CopyConstruct(&at(elem), src);
    return elem;
}


//-----------------------------------------------------------------------------
// Adds multiple elements, uses default constructor
//-----------------------------------------------------------------------------
template<typename T, class A>
inline int CUtlVector<T, A>::AddMultipleToHead(int num) {
    return InsertMultipleBefore(0, num);
}

template<typename T, class A>
inline int CUtlVector<T, A>::AddMultipleToTail(int num) {
    return InsertMultipleBefore(m_Size, num);
}

template<typename T, class A>
inline int CUtlVector<T, A>::AddMultipleToTail(int num, const T* pToCopy) {
    // Can't insert something that's in the list... reallocation may hose us
    Assert((Base() == NULL) || !pToCopy || (pToCopy + num <= Base()) || (pToCopy >= (Base() + Count()) ));

    return InsertMultipleBefore(m_Size, num, pToCopy);
}

template<typename T, class A>
int CUtlVector<T, A>::InsertMultipleAfter(int elem, int num) {
    return InsertMultipleBefore(elem + 1, num);
}


template<typename T, class A>
void CUtlVector<T, A>::SetCount(int count) {
    RemoveAll();
    AddMultipleToTail(count);
}

template<typename T, class A>
inline void CUtlVector<T, A>::SetSize(int size) {
    SetCount(size);
}

template<typename T, class A>
void CUtlVector<T, A>::SetCountNonDestructively(int count) {
    int delta = count - m_Size;
    if (delta > 0) AddMultipleToTail(delta);
    else if (delta < 0) RemoveMultipleFromTail(-delta);
}

template<typename T, class A>
void CUtlVector<T, A>::CopyArray(const T* pArray, int size) {
    // Can't insert something that's in the list... reallocation may hose us
    Assert((Base() == NULL) || !pArray || (Base() >= (pArray + size)) || (pArray >= (Base() + Count()) ));

    SetSize(size);
    for (int i = 0; i < size; i++) {
        (*this)[i] = pArray[i];
    }
}

template<typename T, class A>
void CUtlVector<T, A>::Swap(CUtlVector<T, A> &vec) {
    m_Memory.Swap(vec.m_Memory);
    V_swap(m_Size, vec.m_Size);
    V_swap(m_pElements, vec.m_pElements);
}

template<typename T, class A>
int CUtlVector<T, A>::AddVectorToTail(CUtlVector const &src) {
    Assert(&src != this);

    int base = size();

    // Make space.
    int nSrcCount = src.size();
    EnsureCapacity(base + nSrcCount);

    // Copy the elements.
    m_Size += nSrcCount;
    for (int i = 0; i < nSrcCount; i++) {
        CopyConstruct(&at(base + i), src[i]);
    }
    return base;
}

template<typename T, class A>
inline int CUtlVector<T, A>::InsertMultipleBefore(int elem, int num) {
    if (num == 0)
        return elem;

    // Can insert at the end
    Assert((elem == Count()) || IsValidIndex(elem));

    GrowVector(num);
    ShiftElementsRight(elem, num);

    // Invoke default constructors
    for (int i = 0; i < num; ++i) {
        Construct(&at(elem + i));
    }

    return elem;
}

template<typename T, class A>
inline int CUtlVector<T, A>::InsertMultipleBefore(int elem, int num, const T* pToInsert) {
    if (num == 0)
        return elem;

    // Can insert at the end
    Assert((elem == Count()) || IsValidIndex(elem));

    GrowVector(num);
    ShiftElementsRight(elem, num);

    // Invoke default constructors
    if (!pToInsert) {
        for (int i = 0; i < num; ++i) {
            Construct(&at(elem + i));
        }
    }
    else {
        for (int i = 0; i < num; i++) {
            CopyConstruct(&at(elem + i), pToInsert[i]);
        }
    }

    return elem;
}


//-----------------------------------------------------------------------------
// Finds an element (element needs operator== defined)
//-----------------------------------------------------------------------------
template<typename T, class A>
int CUtlVector<T, A>::Find(const T &src) const {
    for (int i = 0; i < size(); ++i) {
        if (at(i) == src)
            return i;
    }
    return -1;
}

template<typename T, class A>
void CUtlVector<T, A>::FillWithValue(const T &src) {
    for (int i = 0; i < size(); i++) {
        at(i) = src;
    }
}

template<typename T, class A>
bool CUtlVector<T, A>::HasElement(const T &src) const {
    return (Find(src) >= 0);
}


//-----------------------------------------------------------------------------
// Element removal
//-----------------------------------------------------------------------------
template<typename T, class A>
void CUtlVector<T, A>::FastRemove(int elem) {
    Assert(IsValidIndex(elem));

    // Global scope to resolve conflict with Scaleform 4.0
    ::Destruct(&at(elem));
    if (m_Size > 0) {
        if (elem != m_Size - 1)
            memcpy(&at(elem), &at(m_Size - 1), sizeof(T));
        --m_Size;
    }
}

template<typename T, class A>
void CUtlVector<T, A>::Remove(int elem) {
    // Global scope to resolve conflict with Scaleform 4.0
    ::Destruct(&at(elem));
    ShiftElementsLeft(elem);
    --m_Size;
}

template<typename T, class A>
bool CUtlVector<T, A>::FindAndRemove(const T &src) {
    int elem = Find(src);
    if (elem != -1) {
        Remove(elem);
        return true;
    }
    return false;
}

template<typename T, class A>
bool CUtlVector<T, A>::FindAndFastRemove(const T &src) {
    int elem = Find(src);
    if (elem != -1) {
        FastRemove(elem);
        return true;
    }
    return false;
}

template<typename T, class A>
void CUtlVector<T, A>::RemoveMultiple(int elem, int num) {
    Assert(elem >= 0);
    Assert(elem + num <= Count());

    // Global scope to resolve conflict with Scaleform 4.0
    for (int i = elem + num; --i >= elem;)
        ::Destruct(&at(i));

    ShiftElementsLeft(elem, num);
    m_Size -= num;
}

template<typename T, class A>
void CUtlVector<T, A>::RemoveMultipleFromHead(int num) {
    Assert(num <= Count());

    // Global scope to resolve conflict with Scaleform 4.0
    for (int i = num; --i >= 0;)
        ::Destruct(&at(i));

    ShiftElementsLeft(0, num);
    m_Size -= num;
}

template<typename T, class A>
void CUtlVector<T, A>::RemoveMultipleFromTail(int num) {
    Assert(num <= Count());

    // Global scope to resolve conflict with Scaleform 4.0
    for (int i = m_Size - num; i < m_Size; i++)
        ::Destruct(&at(i));

    m_Size -= num;
}

template<typename T, class A>
void CUtlVector<T, A>::RemoveAll() {
    for (int i = m_Size; --i >= 0;) {
        // Global scope to resolve conflict with Scaleform 4.0
        ::Destruct(&at(i));
    }

    m_Size = 0;
}


//-----------------------------------------------------------------------------
// Memory deallocation
//-----------------------------------------------------------------------------

template<typename T, class A>
inline void CUtlVector<T, A>::Purge() {
    RemoveAll();
    m_Memory.Purge();
    ResetDbgInfo();
}


template<typename T, class A>
inline void CUtlVector<T, A>::PurgeAndDeleteElements() {
    for (int i = 0; i < m_Size; i++) {
        delete at(i);
    }
    Purge();
}

template<typename T, class A>
inline void CUtlVector<T, A>::Compact() {
    m_Memory.Purge(m_Size);
}

template<typename T, class A>
inline int CUtlVector<T, A>::NumAllocated() const {
    return m_Memory.NumAllocated();
}
