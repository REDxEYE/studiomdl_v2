//========= Minimal stand-in for tier0/threadtools.h =========================
//
// studiomdl is single threaded and the threading support of tier0 was removed, but a few headers still refer to
// the interlocked/volatile helpers and the mutex types. This header provides just those, built on std::atomic.
//
//=============================================================================

#pragma once
#ifdef _WIN32
#pragma once
#endif

#include <atomic>
#include <cstdint>

typedef uint32_t ThreadId_t;

inline ThreadId_t ThreadGetCurrentId() { return 1; }

inline void ThreadMemoryBarrier() { std::atomic_thread_fence( std::memory_order_seq_cst ); }

template < typename T >
inline T ReadVolatileMemory( T const *pPtr )
{
	volatile const T *pVolatile = pPtr;
	return *pVolatile;
}

//-----------------------------------------------------------------------------
// Mutexes: single threaded, nothing to protect
//-----------------------------------------------------------------------------
class CThreadMutex
{
public:
	void Lock() {}
	void Unlock() {}
	bool TryLock() { return true; }
	bool AssertOwnedByCurrentThread() { return true; }
	void SetTrace( bool ) {}
};

class CThreadFastMutex : public CThreadMutex {};

//-----------------------------------------------------------------------------
// Interlocked integer
//-----------------------------------------------------------------------------
class CInterlockedInt
{
public:
	CInterlockedInt() : m_value( 0 ) {}
	CInterlockedInt( int value ) : m_value( value ) {}
	CInterlockedInt( const CInterlockedInt &other ) : m_value( other.m_value.load() ) {}
	CInterlockedInt &operator=( const CInterlockedInt &other ) { m_value = other.m_value.load(); return *this; }

	operator int() const { return m_value.load(); }
	int operator=( int value ) { m_value = value; return value; }
	int operator++() { return ++m_value; }
	int operator++( int ) { return m_value++; }
	int operator--() { return --m_value; }
	int operator--( int ) { return m_value--; }
	int operator+=( int value ) { return m_value += value; }
	int operator-=( int value ) { return m_value -= value; }

private:
	std::atomic< int > m_value;
};

