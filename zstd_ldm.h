/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */

#ifndef ZSTD1_LDM_H
#define ZSTD1_LDM_H

#if defined (__cplusplus)
extern "C" {
#endif

#include "zstd_compress_internal.h"   /* ldmParams_t, U32 */
#include "zstd.h"   /* ZSTD1_CCtx, size_t */

/*-*************************************
*  Long distance matching
***************************************/

#define ZSTD1_LDM_DEFAULT_WINDOW_LOG ZSTD1_WINDOWLOG_DEFAULTMAX

/**
 * ZSTD1_ldm_generateSequences():
 *
 * Generates the sequences using the long distance match finder.
 * Generates long range matching sequences in `sequences`, which parse a prefix
 * of the source. `sequences` must be large enough to store every sequence,
 * which can be checked with `ZSTD1_ldm_getMaxNbSeq()`.
 * @returns 0 or an error code.
 *
 * NOTE: The user must have called ZSTD1_window_update() for all of the input
 * they have, even if they pass it to ZSTD1_ldm_generateSequences() in chunks.
 * NOTE: This function returns an error if it runs out of space to store
 *       sequences.
 */
size_t ZSTD1_ldm_generateSequences(
            ldmState_t* ldms, rawSeqStore_t* sequences,
            ldmParams_t const* params, void const* src, size_t srcSize);

/**
 * ZSTD1_ldm_blockCompress():
 *
 * Compresses a block using the predefined sequences, along with a secondary
 * block compressor. The literals section of every sequence is passed to the
 * secondary block compressor, and those sequences are interspersed with the
 * predefined sequences. Returns the length of the last literals.
 * Updates `rawSeqStore.pos` to indicate how many sequences have been consumed.
 * `rawSeqStore.seq` may also be updated to split the last sequence between two
 * blocks.
 * @return The length of the last literals.
 *
 * NOTE: The source must be at most the maximum block size, but the predefined
 * sequences can be any size, and may be longer than the block. In the case that
 * they are longer than the block, the last sequences may need to be split into
 * two. We handle that case correctly, and update `rawSeqStore` appropriately.
 * NOTE: This function does not return any errors.
 */
size_t ZSTD1_ldm_blockCompress(rawSeqStore_t* rawSeqStore,
            ZSTD1_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD1_REP_NUM],
            ZSTD1_compressionParameters const* cParams,
            void const* src, size_t srcSize,
            int const extDict);

/**
 * ZSTD1_ldm_skipSequences():
 *
 * Skip past `srcSize` bytes worth of sequences in `rawSeqStore`.
 * Avoids emitting matches less than `minMatch` bytes.
 * Must be called for data with is not passed to ZSTD1_ldm_blockCompress().
 */
void ZSTD1_ldm_skipSequences(rawSeqStore_t* rawSeqStore, size_t srcSize,
    U32 const minMatch);


/** ZSTD1_ldm_getTableSize() :
 *  Estimate the space needed for long distance matching tables or 0 if LDM is
 *  disabled.
 */
size_t ZSTD1_ldm_getTableSize(ldmParams_t params);

/** ZSTD1_ldm_getSeqSpace() :
 *  Return an upper bound on the number of sequences that can be produced by
 *  the long distance matcher, or 0 if LDM is disabled.
 */
size_t ZSTD1_ldm_getMaxNbSeq(ldmParams_t params, size_t maxChunkSize);

/** ZSTD1_ldm_getTableSize() :
 *  Return prime8bytes^(minMatchLength-1) */
U64 ZSTD1_ldm_getHashPower(U32 minMatchLength);

/** ZSTD1_ldm_adjustParameters() :
 *  If the params->hashEveryLog is not set, set it to its default value based on
 *  windowLog and params->hashLog.
 *
 *  Ensures that params->bucketSizeLog is <= params->hashLog (setting it to
 *  params->hashLog if it is not).
 *
 *  Ensures that the minMatchLength >= targetLength during optimal parsing.
 */
void ZSTD1_ldm_adjustParameters(ldmParams_t* params,
                               ZSTD1_compressionParameters const* cParams);

#if defined (__cplusplus)
}
#endif

#endif /* ZSTD1_FAST_H */
