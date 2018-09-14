/**
 * Copyright (c) 2016 Tino Reichardt
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 *
 * You can contact the author at:
 * - zstdmt source repository: https://github.com/mcmilk/zstdmt
 */

/**
 * This file will hold wrapper for systems, which do not support pthreads
 */

/* create fake symbol to avoid empty trnaslation unit warning */
int g_ZSTD1_threading_useles_symbol;

#if defined(ZSTD1_MULTITHREAD) && defined(_WIN32)

/**
 * Windows minimalist Pthread Wrapper, based on :
 * http://www.cse.wustl.edu/~schmidt/win32-cv-1.html
 */


/* ===  Dependencies  === */
#include <process.h>
#include <errno.h>
#include "threading.h"


/* ===  Implementation  === */

static unsigned __stdcall worker(void *arg)
{
    ZSTD1_pthread_t* const thread = (ZSTD1_pthread_t*) arg;
    thread->arg = thread->start_routine(thread->arg);
    return 0;
}

int ZSTD1_pthread_create(ZSTD1_pthread_t* thread, const void* unused,
            void* (*start_routine) (void*), void* arg)
{
    (void)unused;
    thread->arg = arg;
    thread->start_routine = start_routine;
    thread->handle = (HANDLE) _beginthreadex(NULL, 0, worker, thread, 0, NULL);

    if (!thread->handle)
        return errno;
    else
        return 0;
}

int ZSTD1_pthread_join(ZSTD1_pthread_t thread, void **value_ptr)
{
    DWORD result;

    if (!thread.handle) return 0;

    result = WaitForSingleObject(thread.handle, INFINITE);
    switch (result) {
    case WAIT_OBJECT_0:
        if (value_ptr) *value_ptr = thread.arg;
        return 0;
    case WAIT_ABANDONED:
        return EINVAL;
    default:
        return GetLastError();
    }
}

#endif   /* ZSTD1_MULTITHREAD */
