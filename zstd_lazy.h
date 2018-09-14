/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD1_LAZY_H
#define ZSTD1_LAZY_H

#if defined (__cplusplus)
extern "C" {
#endif

#include "zstd_compress_internal.h"

U32 ZSTD1_insertAndFindFirstIndex(
        ZSTD1_matchState_t* ms, ZSTD1_compressionParameters const* cParams,
        const BYTE* ip);

void ZSTD1_preserveUnsortedMark (U32* const table, U32 const size, U32 const reducerValue);  /*! used in ZSTD1_reduceIndex(). pre-emptively increase value of ZSTD1_DUBT_UNSORTED_MARK */

size_t ZSTD1_compressBlock_btlazy2(
        ZSTD1_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD1_REP_NUM],
        ZSTD1_compressionParameters const* cParams, void const* src, size_t srcSize);
size_t ZSTD1_compressBlock_lazy2(
        ZSTD1_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD1_REP_NUM],
        ZSTD1_compressionParameters const* cParams, void const* src, size_t srcSize);
size_t ZSTD1_compressBlock_lazy(
        ZSTD1_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD1_REP_NUM],
        ZSTD1_compressionParameters const* cParams, void const* src, size_t srcSize);
size_t ZSTD1_compressBlock_greedy(
        ZSTD1_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD1_REP_NUM],
        ZSTD1_compressionParameters const* cParams, void const* src, size_t srcSize);

size_t ZSTD1_compressBlock_greedy_extDict(
        ZSTD1_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD1_REP_NUM],
        ZSTD1_compressionParameters const* cParams, void const* src, size_t srcSize);
size_t ZSTD1_compressBlock_lazy_extDict(
        ZSTD1_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD1_REP_NUM],
        ZSTD1_compressionParameters const* cParams, void const* src, size_t srcSize);
size_t ZSTD1_compressBlock_lazy2_extDict(
        ZSTD1_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD1_REP_NUM],
        ZSTD1_compressionParameters const* cParams, void const* src, size_t srcSize);
size_t ZSTD1_compressBlock_btlazy2_extDict(
        ZSTD1_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD1_REP_NUM],
        ZSTD1_compressionParameters const* cParams, void const* src, size_t srcSize);

#if defined (__cplusplus)
}
#endif

#endif /* ZSTD1_LAZY_H */
