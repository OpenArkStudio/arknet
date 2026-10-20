// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <process.h>

#else
#include <unistd.h>

#endif

#ifdef _WIN32
std::string quote_argument(std::string_view argument) {
    // _spawnv joins its arguments into a Windows command line. Preserve spaces,
    // quotes and trailing backslashes for the compiler's C runtime parser.
    std::string quoted = "\"";
    std::size_t backslashes = 0;
    for (const char character : argument)
    {
        if (character == '\\')
        {
            ++backslashes;
            continue;
        }
        quoted.append(character == '"' ? backslashes * 2 + 1 : backslashes, '\\');
        quoted += character;
        backslashes = 0;
    }
    quoted.append(backslashes * 2, '\\');
    return quoted + '"';
}
#endif

int main(int argc, char** argv) {
    const auto* log = std::getenv("ARKNET_TEST_LAUNCHER_LOG");
    if (!log)
    {
        return 2;
    }
    std::ostringstream record;
    int compiler = 1;
    while (compiler < argc && std::string_view(argv[compiler]) == "--record-token")
    {
        if (++compiler == argc)
        {
            return 2;
        }
        record << "token=" << argv[compiler++] << '\n';
    }
    if (compiler == argc)
    {
        return 2;
    }
    record << "compiler=" << argv[compiler] << '\n';
    for (int index = compiler + 1; index < argc; ++index)
    {
        record << "argument=" << argv[index] << '\n';
    }
    {
        std::ofstream output(log, std::ios::app);
        output << record.str();
        output.flush();
        if (!output)
        {
            return 2;
        }
    }
#ifdef _WIN32
    std::vector<std::string> quoted;
    for (int index = compiler; index < argc; ++index)
    {
        quoted.push_back(quote_argument(argv[index]));
    }
    std::vector<const char*> command;
    for (const auto& argument : quoted)
    {
        command.push_back(argument.c_str());
    }
    command.push_back(nullptr);
    const auto result = _spawnv(_P_WAIT, argv[compiler], command.data());
    if (result != -1)
    {
        return static_cast<int>(result);
    }
#else
    execv(argv[compiler], argv + compiler);
#endif
    std::perror("compiler launcher");
    return 127;
}
