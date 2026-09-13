/* hexter - a plain mutex for the engine, on pthreads or on Win32
 *
 * Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.
 *
 * The engine needs a non-recursive mutex with lock, unlock and trylock. POSIX and MinGW
 * have pthreads; MSVC does not, and the standalone application is built with MSVC because
 * clap-wrapper's Windows shell is C++/WinRT. An SRWLOCK in exclusive mode is the same
 * thing there. trylock returns 0 when the lock was taken and EBUSY when it was not, as
 * pthread_mutex_trylock does, which is what the callers test.
 */
#ifndef HEXTER_MUTEX_H
#define HEXTER_MUTEX_H

#include <errno.h>

#if defined(_MSC_VER)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

typedef SRWLOCK hexter_mutex_t;

static inline int hexter_mutex_init(hexter_mutex_t *m)    { InitializeSRWLock(m); return 0; }
static inline int hexter_mutex_destroy(hexter_mutex_t *m) { (void)m; return 0; }
static inline int hexter_mutex_lock(hexter_mutex_t *m)    { AcquireSRWLockExclusive(m); return 0; }
static inline int hexter_mutex_unlock(hexter_mutex_t *m)  { ReleaseSRWLockExclusive(m); return 0; }
static inline int hexter_mutex_trylock(hexter_mutex_t *m) { return TryAcquireSRWLockExclusive(m) ? 0 : EBUSY; }

#else

#include <pthread.h>

typedef pthread_mutex_t hexter_mutex_t;

static inline int hexter_mutex_init(hexter_mutex_t *m)    { return pthread_mutex_init(m, NULL); }
static inline int hexter_mutex_destroy(hexter_mutex_t *m) { return pthread_mutex_destroy(m); }
static inline int hexter_mutex_lock(hexter_mutex_t *m)    { return pthread_mutex_lock(m); }
static inline int hexter_mutex_unlock(hexter_mutex_t *m)  { return pthread_mutex_unlock(m); }
static inline int hexter_mutex_trylock(hexter_mutex_t *m) { return pthread_mutex_trylock(m); }

#endif

#endif /* HEXTER_MUTEX_H */
