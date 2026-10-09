# Troubleshooting

Record the error in the callback where it occurs. `get_last_error()` is
thread-local; an unrelated thread's value does not describe the failed
operation. For sends, record the completion's explicit `error_code` and byte
count. See [Runtime](runtime.md) for the execution and shutdown contracts.

## Diagnose by Symptom

| Symptom | Check first | Next action |
| --- | --- | --- |
| `asio.hpp` is missing | Check that standalone mode is selected and locate the installed header | Set `ARKNET_ASIO_INCLUDE_DIR` to the directory containing `asio.hpp`, then reconfigure; see [installation](guide.md) |
| CMake cannot find Boost or OpenSSL | Check the requested version, installed package and CMake prefix | Set `CMAKE_PREFIX_PATH` or `OPENSSL_ROOT_DIR` to the installed dependency; use a separate build directory when changing providers |
| Asio/Beast socket types do not match | Inspect compile definitions in the verbose build output and check earlier provider includes | Use one provider in all translation units; use the `arknet::arknet` interface target to propagate its definitions |
| Secure headers or OpenSSL symbols are unavailable | Check `ARKNET_ENABLE_SSL` and the OpenSSL headers and link libraries | Enable TLS in CMake and use the exported interface target; for direct headers, define the macro and supply OpenSSL dependencies |
| TLS handshake rejects a certificate | Record the callback error; check the trust chain, certificate validity and requested DNS/IP identity | Load the intended CA and connect using a name or address present in the certificate's subject alternative names; check the system clock |
| A TLS server fails to start or reports `invalid_argument` | Check the error immediately after loading its certificate and private key | Load a usable certificate and matching key before start; for mTLS, also configure the client CA and server verify mode |
| `async_send()` returns `false` with `no_buffer_space` | Inspect the configured limit, queued bytes and outstanding application requests | Pace the producer or reduce concurrency; retry only according to application policy after completions free capacity |
| `send()` returns zero with `in_progress` | Check whether the call ran on a worker of the object's context | Observe the asynchronous completion with `async_send()`; do not wait for a future on that context's workers |
| Timer query returns a default result with `operation_not_supported` or `operation_aborted` | Check whether the call uses a different strand on the same context, or a stopped context | Query on the object's executor or from an owner thread while runners remain active |
| Start, future or shutdown waits indefinitely with external IO | Check that `run()` threads and the work guard are still active | Keep the host context running until objects finish stopping; follow the [shutdown order](runtime.md#lifecycle-and-execution) |
| TCP receive callbacks split or combine application messages | Check the framing configured at both ends | Treat raw TCP as a byte stream, or use matching delimiter/custom framing or `use_dgram` at both ends; see [TCP usage](tcp/usage.md) |
| Docsify is blank when opened from disk | Check the browser console for blocked Markdown or CDN requests | Serve `docs/` over HTTP and check access to the pinned CDN assets |

A send completing without error proves transport completion, not an
application-level acknowledgment. If the peer must confirm processing, record
and validate a response in the application protocol.

## Serve Documentation Locally

From the repository root:

```sh
python3 -m http.server 8000 --directory docs
```

Open [http://127.0.0.1:8000/](http://127.0.0.1:8000/). Docsify fetches Markdown
at runtime, so the documentation must be served over HTTP. If the port is
already in use, choose another port. The language switch at the top preserves
the current page.

## Include Reproduction Evidence

When reporting an issue, include:

- Source revision and the relevant minimal client/server or test case.
- OS, architecture, compiler and dependency versions.
- Asio provider, TLS configuration and CMake configure command.
- Owned or external IO, context/lane count and number of `run()` threads.
- Error category, value and message captured where the error occurred.
- For sends, payload size, framing, queue limit and completion byte count.
- Test logs or a benchmark JSON file, with the exact command used.

Use [Testing](testing.md) for current validation evidence and reproduction
commands. Repository certificates and keys are for testing; use your deployment's
trust and identity configuration when diagnosing TLS.
