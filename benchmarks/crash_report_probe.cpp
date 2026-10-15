// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include "crash_report.hpp"

int main() {
    benchmark_diagnostics::install_crash_reporter();
    RaiseException(0xe0424e42, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    return 0;
}
