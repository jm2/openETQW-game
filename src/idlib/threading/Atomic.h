// Copyright (C) 2007 Id Software, Inc.
//

#ifndef __IDLIB_ATOMIC_H__
#define __IDLIB_ATOMIC_H__

class sdAtomic {
public:
	static bool	CompareAndSwap( volatile int* dest, int comperand, int exchange );	// atomic compare and swap, is a memory barrier
};

ID_INLINE bool sdAtomic::CompareAndSwap( volatile int* dest, int comperand, int exchange ) {
#if defined( _XENON ) || defined( _WIN32 )
	return ( comperand == ::InterlockedCompareExchange( reinterpret_cast< volatile LONG* >( dest ), exchange, comperand ) );
#elif defined( __linux__ )
	return __sync_bool_compare_and_swap( dest, comperand, exchange );
#elif defined( MACOS_X ) || defined( __APPLE__ )
	return __sync_bool_compare_and_swap( dest, comperand, exchange );
#endif
}

#endif /* !__IDLIB_ATOMIC_H__ */
