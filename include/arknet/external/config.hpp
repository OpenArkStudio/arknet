// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once
#define ARKNET_DETAIL_JOIN(left, right) left##right
#define ARKNET_JOIN(left, right) ARKNET_DETAIL_JOIN(left, right)
#define ARKNET_DETAIL_STRINGIZE(value) #value
#define ARKNET_STRINGIZE(value) ARKNET_DETAIL_STRINGIZE(value)
