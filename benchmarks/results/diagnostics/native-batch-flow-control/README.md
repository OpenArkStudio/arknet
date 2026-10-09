# Native Batch Flow-Control Diagnostic

[English](README.md) | [中文](README_CN.md)

`arknet-native-max-batch-probes.log` captures a pre-fix failure on 2026-10-10:
standalone callback execution, one io_context/four threads, 4 clients,
65,507-byte messages, 64 messages per batch, 0.2-second warmup and
0.3-second measurement. The process returned 1 with `native TCP traffic timed out`.

The client finished writing its full batch before reading the reply. The server
read and echoed one message at a time; opposing send buffers could fill and
leave both peers waiting for the other to read. Both execution modes were fixed
to read a complete batch, process each fixed-size message, then echo the batch.

This file is historical failure evidence, not a successful measurement. The
[post-fix validation](../../../../tests/results/macmini-m4pro-20261010/native/README.md)
passed 136 parameter probes; standalone Release, Boost Release, ASan/UBSan and
TSan each passed 3 permanent test cases and 20 assertions. The largest batch is
covered by the permanent tests and 8 post-fix probes.
