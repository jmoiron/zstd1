/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD1_ERRORS_H_398273423
#define ZSTD1_ERRORS_H_398273423

#if defined (__cplusplus)
extern "C" {
#endif

/*===== dependency =====*/
#include <stddef.h>   /* size_t */


/* =====   ZSTDERRORLIB_API : control library symbols visibility   ===== */
#ifndef ZSTDERRORLIB_VISIBILITY
#  if defined(__GNUC__) && (__GNUC__ >= 4)
#    define ZSTDERRORLIB_VISIBILITY __attribute__ ((visibility ("default")))
#  else
#    define ZSTDERRORLIB_VISIBILITY
#  endif
#endif
#if defined(ZSTD1_DLL_EXPORT) && (ZSTD1_DLL_EXPORT==1)
#  define ZSTDERRORLIB_API __declspec(dllexport) ZSTDERRORLIB_VISIBILITY
#elif defined(ZSTD1_DLL_IMPORT) && (ZSTD1_DLL_IMPORT==1)
#  define ZSTDERRORLIB_API __declspec(dllimport) ZSTDERRORLIB_VISIBILITY /* It isn't required but allows to generate better code, saving a function pointer load from the IAT and an indirect jump.*/
#else
#  define ZSTDERRORLIB_API ZSTDERRORLIB_VISIBILITY
#endif

/*-*********************************************
 *  Error codes list
 *-*********************************************
 *  Error codes _values_ are pinned down since v1.3.1 only.
 *  Therefore, don't rely on values if you may link to any version < v1.3.1.
 *
 *  Only values < 100 are considered stable.
 *
 *  note 1 : this API shall be used with static linking only.
 *           dynamic linking is not yet officially supported.
 *  note 2 : Prefer relying on the enum than on its value whenever possible
 *           This is the only supported way to use the error list < v1.3.1
 *  note 3 : ZSTD1_isError() is always correct, whatever the library version.
 **********************************************/
typedef enum {
  ZSTD1_error_no_error = 0,
  ZSTD1_error_GENERIC  = 1,
  ZSTD1_error_prefix_unknown                = 10,
  ZSTD1_error_version_unsupported           = 12,
  ZSTD1_error_frameParameter_unsupported    = 14,
  ZSTD1_error_frameParameter_windowTooLarge = 16,
  ZSTD1_error_corruption_detected = 20,
  ZSTD1_error_checksum_wrong      = 22,
  ZSTD1_error_dictionary_corrupted      = 30,
  ZSTD1_error_dictionary_wrong          = 32,
  ZSTD1_error_dictionaryCreation_failed = 34,
  ZSTD1_error_parameter_unsupported   = 40,
  ZSTD1_error_parameter_outOfBound    = 42,
  ZSTD1_error_tableLog_tooLarge       = 44,
  ZSTD1_error_maxSymbolValue_tooLarge = 46,
  ZSTD1_error_maxSymbolValue_tooSmall = 48,
  ZSTD1_error_stage_wrong       = 60,
  ZSTD1_error_init_missing      = 62,
  ZSTD1_error_memory_allocation = 64,
  ZSTD1_error_workSpace_tooSmall= 66,
  ZSTD1_error_dstSize_tooSmall = 70,
  ZSTD1_error_srcSize_wrong    = 72,
  /* following error codes are __NOT STABLE__, they can be removed or changed in future versions */
  ZSTD1_error_frameIndex_tooLarge = 100,
  ZSTD1_error_seekableIO          = 102,
  ZSTD1_error_maxCode = 120  /* never EVER use this value directly, it can change in future versions! Use ZSTD1_isError() instead */
} ZSTD1_ErrorCode;

/*! ZSTD1_getErrorCode() :
    convert a `size_t` function result into a `ZSTD1_ErrorCode` enum type,
    which can be used to compare with enum list published above */
ZSTDERRORLIB_API ZSTD1_ErrorCode ZSTD1_getErrorCode(size_t functionResult);
ZSTDERRORLIB_API const char* ZSTD1_getErrorString(ZSTD1_ErrorCode code);   /**< Same as ZSTD1_getErrorName, but using a `ZSTD1_ErrorCode` enum argument */


#if defined (__cplusplus)
}
#endif

#endif /* ZSTD1_ERRORS_H_398273423 */
