// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <cstdint>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#define ARKNET_OS_MACOS TARGET_OS_OSX
#define ARKNET_OS_IOS TARGET_OS_IPHONE
#else
#define ARKNET_OS_MACOS 0
#define ARKNET_OS_IOS 0
#endif
#if defined(_WIN32)
#define ARKNET_OS_WINDOWS 1
#else
#define ARKNET_OS_WINDOWS 0
#endif
#if defined(__linux__)
#define ARKNET_OS_LINUX 1
#else
#define ARKNET_OS_LINUX 0
#endif
#if defined(__unix__) || defined(__APPLE__)
#define ARKNET_OS_UNIX 1
#else
#define ARKNET_OS_UNIX 0
#endif

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define ARKNET_ENDIAN_BIG_BYTE 1
#define ARKNET_ENDIAN_LITTLE_BYTE 0
#else
#define ARKNET_ENDIAN_BIG_BYTE 0
#define ARKNET_ENDIAN_LITTLE_BYTE 1
#endif
#define ARKNET_ENDIAN_BIG_WORD 0
#define ARKNET_ENDIAN_LITTLE_WORD 0
#if INTPTR_MAX == INT64_MAX
#define ARKNET_ARCH_WORD_BITS 64
#elif INTPTR_MAX == INT32_MAX
#define ARKNET_ARCH_WORD_BITS 32
#else
#define ARKNET_ARCH_WORD_BITS 16
#endif
#define ARKNET_ARCH_WORD_BITS_64 (ARKNET_ARCH_WORD_BITS == 64)
#define ARKNET_ARCH_WORD_BITS_32 (ARKNET_ARCH_WORD_BITS == 32)
#define ARKNET_ARCH_WORD_BITS_16 (ARKNET_ARCH_WORD_BITS == 16)
