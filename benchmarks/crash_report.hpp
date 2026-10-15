// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <dbghelp.h>

namespace benchmark_diagnostics {

inline LONG WINAPI report_exception(EXCEPTION_POINTERS* exception) {
    // DbgHelp is single-threaded, including when several workers fail together.
    static std::atomic_flag reporting = ATOMIC_FLAG_INIT;
    if (reporting.test_and_set())
    {
        return EXCEPTION_EXECUTE_HANDLER;
    }
    std::fprintf(stderr, "Windows exception code=0x%08lx address=%p\n",
        static_cast<unsigned long>(exception->ExceptionRecord->ExceptionCode),
        exception->ExceptionRecord->ExceptionAddress);
    std::fflush(stderr);
    // StackWalk64 updates its context argument; preserve the OS exception record.
    auto context = *exception->ContextRecord;
    STACKFRAME64 frame{};
    DWORD machine{};
#if defined(_M_X64) || defined(__x86_64__)
    machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context.Rip;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrStack.Offset = context.Rsp;
#elif defined(_M_IX86) || defined(__i386__)
    machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = context.Eip;
    frame.AddrFrame.Offset = context.Ebp;
    frame.AddrStack.Offset = context.Esp;
#else
    return EXCEPTION_EXECUTE_HANDLER;
#endif
    frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;
    const auto process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    if (!SymInitialize(process, nullptr, TRUE))
    {
        return EXCEPTION_EXECUTE_HANDLER;
    }
    alignas(SYMBOL_INFO) unsigned char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    for (unsigned index = 0; index < 64 && frame.AddrPC.Offset; ++index)
    {
        DWORD64 displacement{};
        std::fprintf(stderr, "#%u 0x%llx", index, static_cast<unsigned long long>(frame.AddrPC.Offset));
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol))
        {
            std::fprintf(stderr, " %s+0x%llx", symbol->Name, static_cast<unsigned long long>(displacement));
        }
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD line_offset{};
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &line_offset, &line))
        {
            std::fprintf(stderr, " %s:%lu", line.FileName, static_cast<unsigned long>(line.LineNumber));
        }
        std::fputc('\n', stderr);
        if (!StackWalk64(machine, process, GetCurrentThread(), &frame, &context, nullptr, SymFunctionTableAccess64,
                SymGetModuleBase64, nullptr))
        {
            break;
        }
    }
    std::fflush(stderr);
    SymCleanup(process);
    return EXCEPTION_EXECUTE_HANDLER;
}

inline void install_crash_reporter() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(report_exception);
}

} // namespace benchmark_diagnostics
#else
namespace benchmark_diagnostics {

inline void install_crash_reporter() {}

} // namespace benchmark_diagnostics
#endif
