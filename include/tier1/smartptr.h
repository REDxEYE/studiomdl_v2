//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#pragma once




//
//	CPlainAutoPtr
//		is a smart wrapper for a pointer on the stack that performs "delete" upon destruction.
//
//	No reference counting is performed, copying is prohibited "s_p2.Attach( s_p1.Detach() )" should be used
//	for readability and ease of maintenance.
//
//	Auto pointer supports an "arrow" operator for invoking methods on the pointee and a "dereference" operator
//	for getting a pointee reference.
//
//	No automatic casting to bool/ptrtype is performed to avoid bugs and problems (read on "safe bool idiom"
//	if casting to bool or pointer happens to be useful).
//
//	Test for validity with "IsValid", get the pointer with "Get".
//
template < typename T >
class CPlainAutoPtr
{
public:
	explicit CPlainAutoPtr( T *p = nullptr)		: m_p( p ) {}
	~CPlainAutoPtr( void )						{ Delete(); }

public:
	void Delete( void )							{ delete Detach(); }

private:	// Disallow copying, use Detach() instead to avoid ambiguity
	CPlainAutoPtr( CPlainAutoPtr const &x );
	CPlainAutoPtr & operator = ( CPlainAutoPtr const &x );

public:
	void Attach( T *p )							{ m_p = p; }
	T * Detach( void )							{ T * p( m_p ); m_p = NULL; return p; }

public:
	bool IsValid( void ) const					{ return m_p != nullptr; }
	T * Get( void ) const						{ return m_p; }
	T * operator -> ( void ) const				{ return Get(); }
	T & operator *  ( void ) const				{ return *Get(); }

private:
	T * m_p;
};

//
//	CArrayAutoPtr
//		is a smart wrapper for an array pointer on the stack that performs "delete []" upon destruction.
//
//	No reference counting is performed, copying is prohibited "s_p2.Attach( s_p1.Detach() )" should be used
//	for readability and ease of maintenance.
//
//	Auto pointer supports an "indexing" operator for accessing array elements.
//
//	No automatic casting to bool/ptrtype is performed to avoid bugs and problems (read on "safe bool idiom"
//	if casting to bool or pointer happens to be useful).
//
//	Test for validity with "IsValid", get the array pointer with "Get".
//
template < typename T >
class CArrayAutoPtr : public CPlainAutoPtr < T > // Warning: no polymorphic destructor (delete on base class will be a mistake)
{
public:
	explicit CArrayAutoPtr( T *p = nullptr)		{ this->Attach( p ); }
	~CArrayAutoPtr( void )						{ Delete(); }

public:
	void Delete( void )							{ delete [] this->Detach(); }

public:
	T & operator [] ( int k ) const				{ return this->Get()[ k ]; }
};





