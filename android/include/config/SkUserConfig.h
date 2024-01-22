// DO NOT MODIFY! This file is autogenerated by gn_to_bp.py.
// If need to change a define, modify SkUserConfigManual.h
#pragma once
#include "SkUserConfigManual.h"

#ifndef SKSL_ENABLE_TRACING
#define SKSL_ENABLE_TRACING
#endif

#ifndef SK_ANDROID_FRAMEWORK_USE_PERFETTO
#define SK_ANDROID_FRAMEWORK_USE_PERFETTO
#endif

#ifndef SK_CODEC_DECODES_JPEG
#define SK_CODEC_DECODES_JPEG
#endif

#ifndef SK_CODEC_DECODES_JPEG_GAINMAPS
#define SK_CODEC_DECODES_JPEG_GAINMAPS
#endif

#ifndef SK_CODEC_DECODES_PNG
#define SK_CODEC_DECODES_PNG
#endif

#ifndef SK_CODEC_DECODES_RAW
#define SK_CODEC_DECODES_RAW
#endif

#ifndef SK_CODEC_DECODES_WEBP
#define SK_CODEC_DECODES_WEBP
#endif

#ifndef SK_ENABLE_ANDROID_UTILS
#define SK_ENABLE_ANDROID_UTILS
#endif

#ifndef SK_ENABLE_AVX512_OPTS
#define SK_ENABLE_AVX512_OPTS
#endif

#ifndef SK_ENABLE_PRECOMPILE
#define SK_ENABLE_PRECOMPILE
#endif

#ifndef SK_FONTMGR_FREETYPE_EMPTY_AVAILABLE
#define SK_FONTMGR_FREETYPE_EMPTY_AVAILABLE
#endif

#ifndef SK_GAMMA_APPLY_TO_A8
#define SK_GAMMA_APPLY_TO_A8
#endif

#ifndef SK_GAMMA_CONTRAST
#define SK_GAMMA_CONTRAST 0.0
#endif

#ifndef SK_GAMMA_EXPONENT
#define SK_GAMMA_EXPONENT 1.4
#endif

#ifndef SK_GANESH
#define SK_GANESH
#endif

#ifndef SK_GL
#define SK_GL
#endif

#ifndef SK_HAS_HEIF_LIBRARY
#define SK_HAS_HEIF_LIBRARY
#endif

#ifndef SK_HAS_WUFFS_LIBRARY
#define SK_HAS_WUFFS_LIBRARY
#endif

#ifndef SK_SUPPORT_PDF
#define SK_SUPPORT_PDF
#endif

#ifndef SK_TYPEFACE_FACTORY_FREETYPE
#define SK_TYPEFACE_FACTORY_FREETYPE
#endif

#ifndef SK_USE_VMA
#define SK_USE_VMA
#endif

#ifndef SK_VULKAN
#define SK_VULKAN
#endif

#ifndef SK_XML
#define SK_XML
#endif

#ifndef SK_BUILD_FOR_ANDROID
    #error "SK_BUILD_FOR_ANDROID must be defined!"
#endif
#if defined(SK_BUILD_FOR_IOS) || defined(SK_BUILD_FOR_MAC) || \
    defined(SK_BUILD_FOR_UNIX) || defined(SK_BUILD_FOR_WIN)
    #error "Only SK_BUILD_FOR_ANDROID should be defined!"
#endif
