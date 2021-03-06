/*
 * Copyright (c) 2016-present, Przemyslaw Skibinski, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#include "zstd_compress_internal.h"
#include "zstd_opt.h"


#define ZSTD1_LITFREQ_ADD    2   /* scaling factor for litFreq, so that frequencies adapt faster to new stats. Also used for matchSum (?) */
#define ZSTD1_FREQ_DIV       4   /* log factor when using previous stats to init next stats */
#define ZSTD1_MAX_PRICE     (1<<30)


/*-*************************************
*  Price functions for optimal parser
***************************************/
static void ZSTD1_setLog2Prices(optState_t* optPtr)
{
    optPtr->log2litSum = ZSTD1_highbit32(optPtr->litSum+1);
    optPtr->log2litLengthSum = ZSTD1_highbit32(optPtr->litLengthSum+1);
    optPtr->log2matchLengthSum = ZSTD1_highbit32(optPtr->matchLengthSum+1);
    optPtr->log2offCodeSum = ZSTD1_highbit32(optPtr->offCodeSum+1);
}


static void ZSTD1_rescaleFreqs(optState_t* const optPtr,
                              const BYTE* const src, size_t const srcSize)
{
    optPtr->staticPrices = 0;

    if (optPtr->litLengthSum == 0) {  /* first init */
        unsigned u;
        if (srcSize <= 1024) optPtr->staticPrices = 1;

        assert(optPtr->litFreq!=NULL);
        for (u=0; u<=MaxLit; u++)
            optPtr->litFreq[u] = 0;
        for (u=0; u<srcSize; u++)
            optPtr->litFreq[src[u]]++;
        optPtr->litSum = 0;
        for (u=0; u<=MaxLit; u++) {
            optPtr->litFreq[u] = 1 + (optPtr->litFreq[u] >> ZSTD1_FREQ_DIV);
            optPtr->litSum += optPtr->litFreq[u];
        }

        for (u=0; u<=MaxLL; u++)
            optPtr->litLengthFreq[u] = 1;
        optPtr->litLengthSum = MaxLL+1;
        for (u=0; u<=MaxML; u++)
            optPtr->matchLengthFreq[u] = 1;
        optPtr->matchLengthSum = MaxML+1;
        for (u=0; u<=MaxOff; u++)
            optPtr->offCodeFreq[u] = 1;
        optPtr->offCodeSum = (MaxOff+1);

    } else {
        unsigned u;

        optPtr->litSum = 0;
        for (u=0; u<=MaxLit; u++) {
            optPtr->litFreq[u] = 1 + (optPtr->litFreq[u] >> (ZSTD1_FREQ_DIV+1));
            optPtr->litSum += optPtr->litFreq[u];
        }
        optPtr->litLengthSum = 0;
        for (u=0; u<=MaxLL; u++) {
            optPtr->litLengthFreq[u] = 1 + (optPtr->litLengthFreq[u]>>(ZSTD1_FREQ_DIV+1));
            optPtr->litLengthSum += optPtr->litLengthFreq[u];
        }
        optPtr->matchLengthSum = 0;
        for (u=0; u<=MaxML; u++) {
            optPtr->matchLengthFreq[u] = 1 + (optPtr->matchLengthFreq[u]>>ZSTD1_FREQ_DIV);
            optPtr->matchLengthSum += optPtr->matchLengthFreq[u];
        }
        optPtr->offCodeSum = 0;
        for (u=0; u<=MaxOff; u++) {
            optPtr->offCodeFreq[u] = 1 + (optPtr->offCodeFreq[u]>>ZSTD1_FREQ_DIV);
            optPtr->offCodeSum += optPtr->offCodeFreq[u];
        }
    }

    ZSTD1_setLog2Prices(optPtr);
}


/* ZSTD1_rawLiteralsCost() :
 * cost of literals (only) in given segment (which length can be null)
 * does not include cost of literalLength symbol */
static U32 ZSTD1_rawLiteralsCost(const BYTE* const literals, U32 const litLength,
                                const optState_t* const optPtr)
{
    if (optPtr->staticPrices) return (litLength*6);  /* 6 bit per literal - no statistic used */
    if (litLength == 0) return 0;

    /* literals */
    {   U32 u;
        U32 cost = litLength * optPtr->log2litSum;
        for (u=0; u < litLength; u++)
            cost -= ZSTD1_highbit32(optPtr->litFreq[literals[u]]+1);
        return cost;
    }
}

/* ZSTD1_litLengthPrice() :
 * cost of literalLength symbol */
static U32 ZSTD1_litLengthPrice(U32 const litLength, const optState_t* const optPtr)
{
    if (optPtr->staticPrices) return ZSTD1_highbit32((U32)litLength+1);

    /* literal Length */
    {   U32 const llCode = ZSTD1_LLcode(litLength);
        U32 const price = LL_bits[llCode] + optPtr->log2litLengthSum - ZSTD1_highbit32(optPtr->litLengthFreq[llCode]+1);
        return price;
    }
}

/* ZSTD1_litLengthPrice() :
 * cost of the literal part of a sequence,
 * including literals themselves, and literalLength symbol */
static U32 ZSTD1_fullLiteralsCost(const BYTE* const literals, U32 const litLength,
                                 const optState_t* const optPtr)
{
    return ZSTD1_rawLiteralsCost(literals, litLength, optPtr)
         + ZSTD1_litLengthPrice(litLength, optPtr);
}

/* ZSTD1_litLengthContribution() :
 * @return ( cost(litlength) - cost(0) )
 * this value can then be added to rawLiteralsCost()
 * to provide a cost which is directly comparable to a match ending at same position */
static int ZSTD1_litLengthContribution(U32 const litLength, const optState_t* const optPtr)
{
    if (optPtr->staticPrices) return ZSTD1_highbit32(litLength+1);

    /* literal Length */
    {   U32 const llCode = ZSTD1_LLcode(litLength);
        int const contribution = LL_bits[llCode]
                        + ZSTD1_highbit32(optPtr->litLengthFreq[0]+1)
                        - ZSTD1_highbit32(optPtr->litLengthFreq[llCode]+1);
#if 1
        return contribution;
#else
        return MAX(0, contribution); /* sometimes better, sometimes not ... */
#endif
    }
}

/* ZSTD1_literalsContribution() :
 * creates a fake cost for the literals part of a sequence
 * which can be compared to the ending cost of a match
 * should a new match start at this position */
static int ZSTD1_literalsContribution(const BYTE* const literals, U32 const litLength,
                                     const optState_t* const optPtr)
{
    int const contribution = ZSTD1_rawLiteralsCost(literals, litLength, optPtr)
                           + ZSTD1_litLengthContribution(litLength, optPtr);
    return contribution;
}

/* ZSTD1_getMatchPrice() :
 * Provides the cost of the match part (offset + matchLength) of a sequence
 * Must be combined with ZSTD1_fullLiteralsCost() to get the full cost of a sequence.
 * optLevel: when <2, favors small offset for decompression speed (improved cache efficiency) */
FORCE_INLINE_TEMPLATE U32 ZSTD1_getMatchPrice(
                                    U32 const offset, U32 const matchLength,
                                    const optState_t* const optPtr,
                                    int const optLevel)
{
    U32 price;
    U32 const offCode = ZSTD1_highbit32(offset+1);
    U32 const mlBase = matchLength - MINMATCH;
    assert(matchLength >= MINMATCH);

    if (optPtr->staticPrices)  /* fixed scheme, do not use statistics */
        return ZSTD1_highbit32((U32)mlBase+1) + 16 + offCode;

    price = offCode + optPtr->log2offCodeSum - ZSTD1_highbit32(optPtr->offCodeFreq[offCode]+1);
    if ((optLevel<2) /*static*/ && offCode >= 20) price += (offCode-19)*2; /* handicap for long distance offsets, favor decompression speed */

    /* match Length */
    {   U32 const mlCode = ZSTD1_MLcode(mlBase);
        price += ML_bits[mlCode] + optPtr->log2matchLengthSum - ZSTD1_highbit32(optPtr->matchLengthFreq[mlCode]+1);
    }

    DEBUGLOG(8, "ZSTD1_getMatchPrice(ml:%u) = %u", matchLength, price);
    return price;
}

static void ZSTD1_updateStats(optState_t* const optPtr,
                             U32 litLength, const BYTE* literals,
                             U32 offsetCode, U32 matchLength)
{
    /* literals */
    {   U32 u;
        for (u=0; u < litLength; u++)
            optPtr->litFreq[literals[u]] += ZSTD1_LITFREQ_ADD;
        optPtr->litSum += litLength*ZSTD1_LITFREQ_ADD;
    }

    /* literal Length */
    {   U32 const llCode = ZSTD1_LLcode(litLength);
        optPtr->litLengthFreq[llCode]++;
        optPtr->litLengthSum++;
    }

    /* match offset code (0-2=>repCode; 3+=>offset+2) */
    {   U32 const offCode = ZSTD1_highbit32(offsetCode+1);
        assert(offCode <= MaxOff);
        optPtr->offCodeFreq[offCode]++;
        optPtr->offCodeSum++;
    }

    /* match Length */
    {   U32 const mlBase = matchLength - MINMATCH;
        U32 const mlCode = ZSTD1_MLcode(mlBase);
        optPtr->matchLengthFreq[mlCode]++;
        optPtr->matchLengthSum++;
    }
}


/* ZSTD1_readMINMATCH() :
 * function safe only for comparisons
 * assumption : memPtr must be at least 4 bytes before end of buffer */
MEM_STATIC U32 ZSTD1_readMINMATCH(const void* memPtr, U32 length)
{
    switch (length)
    {
    default :
    case 4 : return MEM_read32(memPtr);
    case 3 : if (MEM_isLittleEndian())
                return MEM_read32(memPtr)<<8;
             else
                return MEM_read32(memPtr)>>8;
    }
}


/* Update hashTable3 up to ip (excluded)
   Assumption : always within prefix (i.e. not within extDict) */
static U32 ZSTD1_insertAndFindFirstIndexHash3 (ZSTD1_matchState_t* ms, const BYTE* const ip)
{
    U32* const hashTable3 = ms->hashTable3;
    U32 const hashLog3 = ms->hashLog3;
    const BYTE* const base = ms->window.base;
    U32 idx = ms->nextToUpdate3;
    U32 const target = ms->nextToUpdate3 = (U32)(ip - base);
    size_t const hash3 = ZSTD1_hash3Ptr(ip, hashLog3);
    assert(hashLog3 > 0);

    while(idx < target) {
        hashTable3[ZSTD1_hash3Ptr(base+idx, hashLog3)] = idx;
        idx++;
    }

    return hashTable3[hash3];
}


/*-*************************************
*  Binary Tree search
***************************************/
/** ZSTD1_insertBt1() : add one or multiple positions to tree.
 *  ip : assumed <= iend-8 .
 * @return : nb of positions added */
static U32 ZSTD1_insertBt1(
                ZSTD1_matchState_t* ms, ZSTD1_compressionParameters const* cParams,
                const BYTE* const ip, const BYTE* const iend,
                U32 const mls, U32 const extDict)
{
    U32*   const hashTable = ms->hashTable;
    U32    const hashLog = cParams->hashLog;
    size_t const h  = ZSTD1_hashPtr(ip, hashLog, mls);
    U32*   const bt = ms->chainTable;
    U32    const btLog  = cParams->chainLog - 1;
    U32    const btMask = (1 << btLog) - 1;
    U32 matchIndex = hashTable[h];
    size_t commonLengthSmaller=0, commonLengthLarger=0;
    const BYTE* const base = ms->window.base;
    const BYTE* const dictBase = ms->window.dictBase;
    const U32 dictLimit = ms->window.dictLimit;
    const BYTE* const dictEnd = dictBase + dictLimit;
    const BYTE* const prefixStart = base + dictLimit;
    const BYTE* match;
    const U32 current = (U32)(ip-base);
    const U32 btLow = btMask >= current ? 0 : current - btMask;
    U32* smallerPtr = bt + 2*(current&btMask);
    U32* largerPtr  = smallerPtr + 1;
    U32 dummy32;   /* to be nullified at the end */
    U32 const windowLow = ms->window.lowLimit;
    U32 matchEndIdx = current+8+1;
    size_t bestLength = 8;
    U32 nbCompares = 1U << cParams->searchLog;
#ifdef ZSTD1_C_PREDICT
    U32 predictedSmall = *(bt + 2*((current-1)&btMask) + 0);
    U32 predictedLarge = *(bt + 2*((current-1)&btMask) + 1);
    predictedSmall += (predictedSmall>0);
    predictedLarge += (predictedLarge>0);
#endif /* ZSTD1_C_PREDICT */

    DEBUGLOG(8, "ZSTD1_insertBt1 (%u)", current);

    assert(ip <= iend-8);   /* required for h calculation */
    hashTable[h] = current;   /* Update Hash Table */

    while (nbCompares-- && (matchIndex > windowLow)) {
        U32* const nextPtr = bt + 2*(matchIndex & btMask);
        size_t matchLength = MIN(commonLengthSmaller, commonLengthLarger);   /* guaranteed minimum nb of common bytes */
        assert(matchIndex < current);

#ifdef ZSTD1_C_PREDICT   /* note : can create issues when hlog small <= 11 */
        const U32* predictPtr = bt + 2*((matchIndex-1) & btMask);   /* written this way, as bt is a roll buffer */
        if (matchIndex == predictedSmall) {
            /* no need to check length, result known */
            *smallerPtr = matchIndex;
            if (matchIndex <= btLow) { smallerPtr=&dummy32; break; }   /* beyond tree size, stop the search */
            smallerPtr = nextPtr+1;               /* new "smaller" => larger of match */
            matchIndex = nextPtr[1];              /* new matchIndex larger than previous (closer to current) */
            predictedSmall = predictPtr[1] + (predictPtr[1]>0);
            continue;
        }
        if (matchIndex == predictedLarge) {
            *largerPtr = matchIndex;
            if (matchIndex <= btLow) { largerPtr=&dummy32; break; }   /* beyond tree size, stop the search */
            largerPtr = nextPtr;
            matchIndex = nextPtr[0];
            predictedLarge = predictPtr[0] + (predictPtr[0]>0);
            continue;
        }
#endif

        if ((!extDict) || (matchIndex+matchLength >= dictLimit)) {
            assert(matchIndex+matchLength >= dictLimit);   /* might be wrong if extDict is incorrectly set to 0 */
            match = base + matchIndex;
            matchLength += ZSTD1_count(ip+matchLength, match+matchLength, iend);
        } else {
            match = dictBase + matchIndex;
            matchLength += ZSTD1_count_2segments(ip+matchLength, match+matchLength, iend, dictEnd, prefixStart);
            if (matchIndex+matchLength >= dictLimit)
                match = base + matchIndex;   /* to prepare for next usage of match[matchLength] */
        }

        if (matchLength > bestLength) {
            bestLength = matchLength;
            if (matchLength > matchEndIdx - matchIndex)
                matchEndIdx = matchIndex + (U32)matchLength;
        }

        if (ip+matchLength == iend) {   /* equal : no way to know if inf or sup */
            break;   /* drop , to guarantee consistency ; miss a bit of compression, but other solutions can corrupt tree */
        }

        if (match[matchLength] < ip[matchLength]) {  /* necessarily within buffer */
            /* match is smaller than current */
            *smallerPtr = matchIndex;             /* update smaller idx */
            commonLengthSmaller = matchLength;    /* all smaller will now have at least this guaranteed common length */
            if (matchIndex <= btLow) { smallerPtr=&dummy32; break; }   /* beyond tree size, stop searching */
            smallerPtr = nextPtr+1;               /* new "candidate" => larger than match, which was smaller than target */
            matchIndex = nextPtr[1];              /* new matchIndex, larger than previous and closer to current */
        } else {
            /* match is larger than current */
            *largerPtr = matchIndex;
            commonLengthLarger = matchLength;
            if (matchIndex <= btLow) { largerPtr=&dummy32; break; }   /* beyond tree size, stop searching */
            largerPtr = nextPtr;
            matchIndex = nextPtr[0];
    }   }

    *smallerPtr = *largerPtr = 0;
    if (bestLength > 384) return MIN(192, (U32)(bestLength - 384));   /* speed optimization */
    assert(matchEndIdx > current + 8);
    return matchEndIdx - (current + 8);
}

FORCE_INLINE_TEMPLATE
void ZSTD1_updateTree_internal(
                ZSTD1_matchState_t* ms, ZSTD1_compressionParameters const* cParams,
                const BYTE* const ip, const BYTE* const iend,
                const U32 mls, const U32 extDict)
{
    const BYTE* const base = ms->window.base;
    U32 const target = (U32)(ip - base);
    U32 idx = ms->nextToUpdate;
    DEBUGLOG(7, "ZSTD1_updateTree_internal, from %u to %u  (extDict:%u)",
                idx, target, extDict);

    while(idx < target)
        idx += ZSTD1_insertBt1(ms, cParams, base+idx, iend, mls, extDict);
    ms->nextToUpdate = target;
}

void ZSTD1_updateTree(
                ZSTD1_matchState_t* ms, ZSTD1_compressionParameters const* cParams,
                const BYTE* ip, const BYTE* iend)
{
    ZSTD1_updateTree_internal(ms, cParams, ip, iend, cParams->searchLength, 0 /*extDict*/);
}

FORCE_INLINE_TEMPLATE
U32 ZSTD1_insertBtAndGetAllMatches (
                    ZSTD1_matchState_t* ms, ZSTD1_compressionParameters const* cParams,
                    const BYTE* const ip, const BYTE* const iLimit, int const extDict,
                    U32 rep[ZSTD1_REP_NUM], U32 const ll0,
                    ZSTD1_match_t* matches, const U32 lengthToBeat, U32 const mls /* template */)
{
    U32 const sufficient_len = MIN(cParams->targetLength, ZSTD1_OPT_NUM -1);
    const BYTE* const base = ms->window.base;
    U32 const current = (U32)(ip-base);
    U32 const hashLog = cParams->hashLog;
    U32 const minMatch = (mls==3) ? 3 : 4;
    U32* const hashTable = ms->hashTable;
    size_t const h  = ZSTD1_hashPtr(ip, hashLog, mls);
    U32 matchIndex  = hashTable[h];
    U32* const bt   = ms->chainTable;
    U32 const btLog = cParams->chainLog - 1;
    U32 const btMask= (1U << btLog) - 1;
    size_t commonLengthSmaller=0, commonLengthLarger=0;
    const BYTE* const dictBase = ms->window.dictBase;
    U32 const dictLimit = ms->window.dictLimit;
    const BYTE* const dictEnd = dictBase + dictLimit;
    const BYTE* const prefixStart = base + dictLimit;
    U32 const btLow = btMask >= current ? 0 : current - btMask;
    U32 const windowLow = ms->window.lowLimit;
    U32* smallerPtr = bt + 2*(current&btMask);
    U32* largerPtr  = bt + 2*(current&btMask) + 1;
    U32 matchEndIdx = current+8+1;   /* farthest referenced position of any match => detects repetitive patterns */
    U32 dummy32;   /* to be nullified at the end */
    U32 mnum = 0;
    U32 nbCompares = 1U << cParams->searchLog;

    size_t bestLength = lengthToBeat-1;
    DEBUGLOG(7, "ZSTD1_insertBtAndGetAllMatches");

    /* check repCode */
    {   U32 const lastR = ZSTD1_REP_NUM + ll0;
        U32 repCode;
        for (repCode = ll0; repCode < lastR; repCode++) {
            U32 const repOffset = (repCode==ZSTD1_REP_NUM) ? (rep[0] - 1) : rep[repCode];
            U32 const repIndex = current - repOffset;
            U32 repLen = 0;
            assert(current >= dictLimit);
            if (repOffset-1 /* intentional overflow, discards 0 and -1 */ < current-dictLimit) {  /* equivalent to `current > repIndex >= dictLimit` */
                if (ZSTD1_readMINMATCH(ip, minMatch) == ZSTD1_readMINMATCH(ip - repOffset, minMatch)) {
                    repLen = (U32)ZSTD1_count(ip+minMatch, ip+minMatch-repOffset, iLimit) + minMatch;
                }
            } else {  /* repIndex < dictLimit || repIndex >= current */
                const BYTE* const repMatch = dictBase + repIndex;
                assert(current >= windowLow);
                if ( extDict /* this case only valid in extDict mode */
                  && ( ((repOffset-1) /*intentional overflow*/ < current - windowLow)  /* equivalent to `current > repIndex >= windowLow` */
                     & (((U32)((dictLimit-1) - repIndex) >= 3) ) /* intentional overflow : do not test positions overlapping 2 memory segments */)
                  && (ZSTD1_readMINMATCH(ip, minMatch) == ZSTD1_readMINMATCH(repMatch, minMatch)) ) {
                    repLen = (U32)ZSTD1_count_2segments(ip+minMatch, repMatch+minMatch, iLimit, dictEnd, prefixStart) + minMatch;
            }   }
            /* save longer solution */
            if (repLen > bestLength) {
                DEBUGLOG(8, "found rep-match %u of length %u",
                            repCode - ll0, (U32)repLen);
                bestLength = repLen;
                matches[mnum].off = repCode - ll0;
                matches[mnum].len = (U32)repLen;
                mnum++;
                if ( (repLen > sufficient_len)
                   | (ip+repLen == iLimit) ) {  /* best possible */
                    return mnum;
    }   }   }   }

    /* HC3 match finder */
    if ((mls == 3) /*static*/ && (bestLength < mls)) {
        U32 const matchIndex3 = ZSTD1_insertAndFindFirstIndexHash3(ms, ip);
        if ((matchIndex3 > windowLow)
          & (current - matchIndex3 < (1<<18)) /*heuristic : longer distance likely too expensive*/ ) {
            size_t mlen;
            if ((!extDict) /*static*/ || (matchIndex3 >= dictLimit)) {
                const BYTE* const match = base + matchIndex3;
                mlen = ZSTD1_count(ip, match, iLimit);
            } else {
                const BYTE* const match = dictBase + matchIndex3;
                mlen = ZSTD1_count_2segments(ip, match, iLimit, dictEnd, prefixStart);
            }

            /* save best solution */
            if (mlen >= mls /* == 3 > bestLength */) {
                DEBUGLOG(8, "found small match with hlog3, of length %u",
                            (U32)mlen);
                bestLength = mlen;
                assert(current > matchIndex3);
                assert(mnum==0);  /* no prior solution */
                matches[0].off = (current - matchIndex3) + ZSTD1_REP_MOVE;
                matches[0].len = (U32)mlen;
                mnum = 1;
                if ( (mlen > sufficient_len) |
                     (ip+mlen == iLimit) ) {  /* best possible length */
                    ms->nextToUpdate = current+1;  /* skip insertion */
                    return 1;
    }   }   }   }

    hashTable[h] = current;   /* Update Hash Table */

    while (nbCompares-- && (matchIndex > windowLow)) {
        U32* const nextPtr = bt + 2*(matchIndex & btMask);
        size_t matchLength = MIN(commonLengthSmaller, commonLengthLarger);   /* guaranteed minimum nb of common bytes */
        const BYTE* match;
        assert(current > matchIndex);

        if ((!extDict) || (matchIndex+matchLength >= dictLimit)) {
            assert(matchIndex+matchLength >= dictLimit);  /* ensure the condition is correct when !extDict */
            match = base + matchIndex;
            matchLength += ZSTD1_count(ip+matchLength, match+matchLength, iLimit);
        } else {
            match = dictBase + matchIndex;
            matchLength += ZSTD1_count_2segments(ip+matchLength, match+matchLength, iLimit, dictEnd, prefixStart);
            if (matchIndex+matchLength >= dictLimit)
                match = base + matchIndex;   /* prepare for match[matchLength] */
        }

        if (matchLength > bestLength) {
            DEBUGLOG(8, "found match of length %u at distance %u",
                    (U32)matchLength, current - matchIndex);
            assert(matchEndIdx > matchIndex);
            if (matchLength > matchEndIdx - matchIndex)
                matchEndIdx = matchIndex + (U32)matchLength;
            bestLength = matchLength;
            matches[mnum].off = (current - matchIndex) + ZSTD1_REP_MOVE;
            matches[mnum].len = (U32)matchLength;
            mnum++;
            if (matchLength > ZSTD1_OPT_NUM) break;
            if (ip+matchLength == iLimit) {  /* equal : no way to know if inf or sup */
                break;   /* drop, to preserve bt consistency (miss a little bit of compression) */
            }
        }

        if (match[matchLength] < ip[matchLength]) {
            /* match smaller than current */
            *smallerPtr = matchIndex;             /* update smaller idx */
            commonLengthSmaller = matchLength;    /* all smaller will now have at least this guaranteed common length */
            if (matchIndex <= btLow) { smallerPtr=&dummy32; break; }   /* beyond tree size, stop the search */
            smallerPtr = nextPtr+1;               /* new candidate => larger than match, which was smaller than current */
            matchIndex = nextPtr[1];              /* new matchIndex, larger than previous, closer to current */
        } else {
            *largerPtr = matchIndex;
            commonLengthLarger = matchLength;
            if (matchIndex <= btLow) { largerPtr=&dummy32; break; }   /* beyond tree size, stop the search */
            largerPtr = nextPtr;
            matchIndex = nextPtr[0];
    }   }

    *smallerPtr = *largerPtr = 0;

    assert(matchEndIdx > current+8);
    ms->nextToUpdate = matchEndIdx - 8;  /* skip repetitive patterns */
    return mnum;
}


FORCE_INLINE_TEMPLATE U32 ZSTD1_BtGetAllMatches (
                        ZSTD1_matchState_t* ms, ZSTD1_compressionParameters const* cParams,
                        const BYTE* ip, const BYTE* const iHighLimit, int const extDict,
                        U32 rep[ZSTD1_REP_NUM], U32 const ll0,
                        ZSTD1_match_t* matches, U32 const lengthToBeat)
{
    U32 const matchLengthSearch = cParams->searchLength;
    DEBUGLOG(7, "ZSTD1_BtGetAllMatches");
    if (ip < ms->window.base + ms->nextToUpdate) return 0;   /* skipped area */
    ZSTD1_updateTree_internal(ms, cParams, ip, iHighLimit, matchLengthSearch, extDict);
    switch(matchLengthSearch)
    {
    case 3 : return ZSTD1_insertBtAndGetAllMatches(ms, cParams, ip, iHighLimit, extDict, rep, ll0, matches, lengthToBeat, 3);
    default :
    case 4 : return ZSTD1_insertBtAndGetAllMatches(ms, cParams, ip, iHighLimit, extDict, rep, ll0, matches, lengthToBeat, 4);
    case 5 : return ZSTD1_insertBtAndGetAllMatches(ms, cParams, ip, iHighLimit, extDict, rep, ll0, matches, lengthToBeat, 5);
    case 7 :
    case 6 : return ZSTD1_insertBtAndGetAllMatches(ms, cParams, ip, iHighLimit, extDict, rep, ll0, matches, lengthToBeat, 6);
    }
}


/*-*******************************
*  Optimal parser
*********************************/
typedef struct repcodes_s {
    U32 rep[3];
} repcodes_t;

repcodes_t ZSTD1_updateRep(U32 const rep[3], U32 const offset, U32 const ll0)
{
    repcodes_t newReps;
    if (offset >= ZSTD1_REP_NUM) {  /* full offset */
        newReps.rep[2] = rep[1];
        newReps.rep[1] = rep[0];
        newReps.rep[0] = offset - ZSTD1_REP_MOVE;
    } else {   /* repcode */
        U32 const repCode = offset + ll0;
        if (repCode > 0) {  /* note : if repCode==0, no change */
            U32 const currentOffset = (repCode==ZSTD1_REP_NUM) ? (rep[0] - 1) : rep[repCode];
            newReps.rep[2] = (repCode >= 2) ? rep[1] : rep[2];
            newReps.rep[1] = rep[0];
            newReps.rep[0] = currentOffset;
        } else {   /* repCode == 0 */
            memcpy(&newReps, rep, sizeof(newReps));
        }
    }
    return newReps;
}


typedef struct {
    const BYTE* anchor;
    U32 litlen;
    U32 rawLitCost;
} cachedLiteralPrice_t;

static U32 ZSTD1_rawLiteralsCost_cached(
                            cachedLiteralPrice_t* const cachedLitPrice,
                            const BYTE* const anchor, U32 const litlen,
                            const optState_t* const optStatePtr)
{
    U32 startCost;
    U32 remainingLength;
    const BYTE* startPosition;

    if (anchor == cachedLitPrice->anchor) {
        startCost = cachedLitPrice->rawLitCost;
        startPosition = anchor + cachedLitPrice->litlen;
        assert(litlen >= cachedLitPrice->litlen);
        remainingLength = litlen - cachedLitPrice->litlen;
    } else {
        startCost = 0;
        startPosition = anchor;
        remainingLength = litlen;
    }

    {   U32 const rawLitCost = startCost + ZSTD1_rawLiteralsCost(startPosition, remainingLength, optStatePtr);
        cachedLitPrice->anchor = anchor;
        cachedLitPrice->litlen = litlen;
        cachedLitPrice->rawLitCost = rawLitCost;
        return rawLitCost;
    }
}

static U32 ZSTD1_fullLiteralsCost_cached(
                            cachedLiteralPrice_t* const cachedLitPrice,
                            const BYTE* const anchor, U32 const litlen,
                            const optState_t* const optStatePtr)
{
    return ZSTD1_rawLiteralsCost_cached(cachedLitPrice, anchor, litlen, optStatePtr)
         + ZSTD1_litLengthPrice(litlen, optStatePtr);
}

static int ZSTD1_literalsContribution_cached(
                            cachedLiteralPrice_t* const cachedLitPrice,
                            const BYTE* const anchor, U32 const litlen,
                            const optState_t* const optStatePtr)
{
    int const contribution = ZSTD1_rawLiteralsCost_cached(cachedLitPrice, anchor, litlen, optStatePtr)
                           + ZSTD1_litLengthContribution(litlen, optStatePtr);
    return contribution;
}

FORCE_INLINE_TEMPLATE
size_t ZSTD1_compressBlock_opt_generic(ZSTD1_matchState_t* ms,seqStore_t* seqStore,
                                      U32 rep[ZSTD1_REP_NUM],
                                      ZSTD1_compressionParameters const* cParams,
                                      const void* src, size_t srcSize,
                                      const int optLevel, const int extDict)
{
    optState_t* const optStatePtr = &ms->opt;
    const BYTE* const istart = (const BYTE*)src;
    const BYTE* ip = istart;
    const BYTE* anchor = istart;
    const BYTE* const iend = istart + srcSize;
    const BYTE* const ilimit = iend - 8;
    const BYTE* const base = ms->window.base;
    const BYTE* const prefixStart = base + ms->window.dictLimit;

    U32 const sufficient_len = MIN(cParams->targetLength, ZSTD1_OPT_NUM -1);
    U32 const minMatch = (cParams->searchLength == 3) ? 3 : 4;

    ZSTD1_optimal_t* const opt = optStatePtr->priceTable;
    ZSTD1_match_t* const matches = optStatePtr->matchTable;
    cachedLiteralPrice_t cachedLitPrice;

    /* init */
    DEBUGLOG(5, "ZSTD1_compressBlock_opt_generic");
    ms->nextToUpdate3 = ms->nextToUpdate;
    ZSTD1_rescaleFreqs(optStatePtr, (const BYTE*)src, srcSize);
    ip += (ip==prefixStart);
    memset(&cachedLitPrice, 0, sizeof(cachedLitPrice));

    /* Match Loop */
    while (ip < ilimit) {
        U32 cur, last_pos = 0;
        U32 best_mlen, best_off;

        /* find first match */
        {   U32 const litlen = (U32)(ip - anchor);
            U32 const ll0 = !litlen;
            U32 const nbMatches = ZSTD1_BtGetAllMatches(ms, cParams, ip, iend, extDict, rep, ll0, matches, minMatch);
            if (!nbMatches) { ip++; continue; }

            /* initialize opt[0] */
            { U32 i ; for (i=0; i<ZSTD1_REP_NUM; i++) opt[0].rep[i] = rep[i]; }
            opt[0].mlen = 1;
            opt[0].litlen = litlen;

            /* large match -> immediate encoding */
            {   U32 const maxML = matches[nbMatches-1].len;
                DEBUGLOG(7, "found %u matches of maxLength=%u and offset=%u at cPos=%u => start new serie",
                            nbMatches, maxML, matches[nbMatches-1].off, (U32)(ip-prefixStart));

                if (maxML > sufficient_len) {
                    best_mlen = maxML;
                    best_off = matches[nbMatches-1].off;
                    DEBUGLOG(7, "large match (%u>%u), immediate encoding",
                                best_mlen, sufficient_len);
                    cur = 0;
                    last_pos = 1;
                    goto _shortestPath;
            }   }

            /* set prices for first matches starting position == 0 */
            {   U32 const literalsPrice = ZSTD1_fullLiteralsCost_cached(&cachedLitPrice, anchor, litlen, optStatePtr);
                U32 pos;
                U32 matchNb;
                for (pos = 0; pos < minMatch; pos++) {
                    opt[pos].mlen = 1;
                    opt[pos].price = ZSTD1_MAX_PRICE;
                }
                for (matchNb = 0; matchNb < nbMatches; matchNb++) {
                    U32 const offset = matches[matchNb].off;
                    U32 const end = matches[matchNb].len;
                    repcodes_t const repHistory = ZSTD1_updateRep(rep, offset, ll0);
                    for ( ; pos <= end ; pos++ ) {
                        U32 const matchPrice = literalsPrice + ZSTD1_getMatchPrice(offset, pos, optStatePtr, optLevel);
                        DEBUGLOG(7, "rPos:%u => set initial price : %u",
                                    pos, matchPrice);
                        opt[pos].mlen = pos;
                        opt[pos].off = offset;
                        opt[pos].litlen = litlen;
                        opt[pos].price = matchPrice;
                        memcpy(opt[pos].rep, &repHistory, sizeof(repHistory));
                }   }
                last_pos = pos-1;
            }
        }

        /* check further positions */
        for (cur = 1; cur <= last_pos; cur++) {
            const BYTE* const inr = ip + cur;
            assert(cur < ZSTD1_OPT_NUM);

            /* Fix current position with one literal if cheaper */
            {   U32 const litlen = (opt[cur-1].mlen == 1) ? opt[cur-1].litlen + 1 : 1;
                int price;  /* note : contribution can be negative */
                if (cur > litlen) {
                    price = opt[cur - litlen].price + ZSTD1_literalsContribution(inr-litlen, litlen, optStatePtr);
                } else {
                    price = ZSTD1_literalsContribution_cached(&cachedLitPrice, anchor, litlen, optStatePtr);
                }
                assert(price < 1000000000); /* overflow check */
                if (price <= opt[cur].price) {
                    DEBUGLOG(7, "rPos:%u : better price (%u<%u) using literal",
                                cur, price, opt[cur].price);
                    opt[cur].mlen = 1;
                    opt[cur].off = 0;
                    opt[cur].litlen = litlen;
                    opt[cur].price = price;
                    memcpy(opt[cur].rep, opt[cur-1].rep, sizeof(opt[cur].rep));
            }   }

            /* last match must start at a minimum distance of 8 from oend */
            if (inr > ilimit) continue;

            if (cur == last_pos) break;

             if ( (optLevel==0) /*static*/
               && (opt[cur+1].price <= opt[cur].price) )
                continue;  /* skip unpromising positions; about ~+6% speed, -0.01 ratio */

            {   U32 const ll0 = (opt[cur].mlen != 1);
                U32 const litlen = (opt[cur].mlen == 1) ? opt[cur].litlen : 0;
                U32 const previousPrice = (cur > litlen) ? opt[cur-litlen].price : 0;
                U32 const basePrice = previousPrice + ZSTD1_fullLiteralsCost(inr-litlen, litlen, optStatePtr);
                U32 const nbMatches = ZSTD1_BtGetAllMatches(ms, cParams, inr, iend, extDict, opt[cur].rep, ll0, matches, minMatch);
                U32 matchNb;
                if (!nbMatches) continue;

                {   U32 const maxML = matches[nbMatches-1].len;
                    DEBUGLOG(7, "rPos:%u, found %u matches, of maxLength=%u",
                                cur, nbMatches, maxML);

                    if ( (maxML > sufficient_len)
                       | (cur + maxML >= ZSTD1_OPT_NUM) ) {
                        best_mlen = maxML;
                        best_off = matches[nbMatches-1].off;
                        last_pos = cur + 1;
                        goto _shortestPath;
                    }
                }

                /* set prices using matches found at position == cur */
                for (matchNb = 0; matchNb < nbMatches; matchNb++) {
                    U32 const offset = matches[matchNb].off;
                    repcodes_t const repHistory = ZSTD1_updateRep(opt[cur].rep, offset, ll0);
                    U32 const lastML = matches[matchNb].len;
                    U32 const startML = (matchNb>0) ? matches[matchNb-1].len+1 : minMatch;
                    U32 mlen;

                    DEBUGLOG(7, "testing match %u => offCode=%u, mlen=%u, llen=%u",
                                matchNb, matches[matchNb].off, lastML, litlen);

                    for (mlen = lastML; mlen >= startML; mlen--) {
                        U32 const pos = cur + mlen;
                        int const price = basePrice + ZSTD1_getMatchPrice(offset, mlen, optStatePtr, optLevel);

                        if ((pos > last_pos) || (price < opt[pos].price)) {
                            DEBUGLOG(7, "rPos:%u => new better price (%u<%u)",
                                        pos, price, opt[pos].price);
                            while (last_pos < pos) { opt[last_pos+1].price = ZSTD1_MAX_PRICE; last_pos++; }
                            opt[pos].mlen = mlen;
                            opt[pos].off = offset;
                            opt[pos].litlen = litlen;
                            opt[pos].price = price;
                            memcpy(opt[pos].rep, &repHistory, sizeof(repHistory));
                        } else {
                            if (optLevel==0) break;  /* gets ~+10% speed for about -0.01 ratio loss */
                        }
            }   }   }
        }  /* for (cur = 1; cur <= last_pos; cur++) */

        best_mlen = opt[last_pos].mlen;
        best_off = opt[last_pos].off;
        cur = last_pos - best_mlen;

_shortestPath:   /* cur, last_pos, best_mlen, best_off have to be set */
        assert(opt[0].mlen == 1);

        /* reverse traversal */
        DEBUGLOG(7, "start reverse traversal (last_pos:%u, cur:%u)",
                    last_pos, cur);
        {   U32 selectedMatchLength = best_mlen;
            U32 selectedOffset = best_off;
            U32 pos = cur;
            while (1) {
                U32 const mlen = opt[pos].mlen;
                U32 const off = opt[pos].off;
                opt[pos].mlen = selectedMatchLength;
                opt[pos].off = selectedOffset;
                selectedMatchLength = mlen;
                selectedOffset = off;
                if (mlen > pos) break;
                pos -= mlen;
        }   }

        /* save sequences */
        {   U32 pos;
            for (pos=0; pos < last_pos; ) {
                U32 const llen = (U32)(ip - anchor);
                U32 const mlen = opt[pos].mlen;
                U32 const offset = opt[pos].off;
                if (mlen == 1) { ip++; pos++; continue; }  /* literal position => move on */
                pos += mlen; ip += mlen;

                /* repcodes update : like ZSTD1_updateRep(), but update in place */
                if (offset >= ZSTD1_REP_NUM) {  /* full offset */
                    rep[2] = rep[1];
                    rep[1] = rep[0];
                    rep[0] = offset - ZSTD1_REP_MOVE;
                } else {   /* repcode */
                    U32 const repCode = offset + (llen==0);
                    if (repCode) {  /* note : if repCode==0, no change */
                        U32 const currentOffset = (repCode==ZSTD1_REP_NUM) ? (rep[0] - 1) : rep[repCode];
                        if (repCode >= 2) rep[2] = rep[1];
                        rep[1] = rep[0];
                        rep[0] = currentOffset;
                    }
                }

                ZSTD1_updateStats(optStatePtr, llen, anchor, offset, mlen);
                ZSTD1_storeSeq(seqStore, llen, anchor, offset, mlen-MINMATCH);
                anchor = ip;
        }   }
        ZSTD1_setLog2Prices(optStatePtr);
    }   /* while (ip < ilimit) */

    /* Return the last literals size */
    return iend - anchor;
}


size_t ZSTD1_compressBlock_btopt(
        ZSTD1_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD1_REP_NUM],
        ZSTD1_compressionParameters const* cParams, void const* src, size_t srcSize)
{
    DEBUGLOG(5, "ZSTD1_compressBlock_btopt");
    return ZSTD1_compressBlock_opt_generic(ms, seqStore, rep, cParams, src, srcSize, 0 /*optLevel*/, 0 /*extDict*/);
}

size_t ZSTD1_compressBlock_btultra(
        ZSTD1_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD1_REP_NUM],
        ZSTD1_compressionParameters const* cParams, void const* src, size_t srcSize)
{
    return ZSTD1_compressBlock_opt_generic(ms, seqStore, rep, cParams, src, srcSize, 2 /*optLevel*/, 0 /*extDict*/);
}

size_t ZSTD1_compressBlock_btopt_extDict(
        ZSTD1_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD1_REP_NUM],
        ZSTD1_compressionParameters const* cParams, void const* src, size_t srcSize)
{
    return ZSTD1_compressBlock_opt_generic(ms, seqStore, rep, cParams, src, srcSize, 0 /*optLevel*/, 1 /*extDict*/);
}

size_t ZSTD1_compressBlock_btultra_extDict(
        ZSTD1_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD1_REP_NUM],
        ZSTD1_compressionParameters const* cParams, void const* src, size_t srcSize)
{
    return ZSTD1_compressBlock_opt_generic(ms, seqStore, rep, cParams, src, srcSize, 2 /*optLevel*/, 1 /*extDict*/);
}
