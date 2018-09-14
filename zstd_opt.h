/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD1_OPT_H
#define ZSTD1_OPT_H

#if defined (__cplusplus)
extern "C" {
#endif

#include "zstd_compress_internal.h"

void ZSTD1_updateTree(
        ZSTD1_matchState_t* ms, ZSTD1_compressionParameters const* cParams,
        const BYTE* ip, const BYTE* iend);  /* used in ZSTD1_loadDictionaryContent() */

size_t ZSTD1_compressBlock_btopt(
        ZSTD1_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD1_REP_NUM],
        ZSTD1_compressionParameters const* cParams, void const* src, size_t srcSize);
size_t ZSTD1_compressBlock_btultra(
        ZSTD1_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD1_REP_NUM],
        ZSTD1_compressionParameters const* cParams, void const* src, size_t srcSize);

size_t ZSTD1_compressBlock_btopt_extDict(
        ZSTD1_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD1_REP_NUM],
        ZSTD1_compressionParameters const* cParams, void const* src, size_t srcSize);
size_t ZSTD1_compressBlock_btultra_extDict(
        ZSTD1_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD1_REP_NUM],
        ZSTD1_compressionParameters const* cParams, void const* src, size_t srcSize);

#if defined (__cplusplus)
}
#endif

#endif /* ZSTD1_OPT_H */
