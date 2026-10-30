# Troubleshooting

Record thread-local `get_last_error()` where it occurs; another thread's value
does not describe the failure. For sends, record the completion's explicit
`error_code` and byte count. See [Runtime](../concepts/lifecycle.md) for execution and shutdown.

## Diagnose by Symptom

| Symptom | Check first | Next action |
| --- | --- | --- |
| `asio.hpp` is missing | Standalone Asio selected; installed header location | Set `ARKNET_ASIO_INCLUDE_DIR` to the directory containing `asio.hpp`, then reconfigure; see [installation](../get-started/installation.md) |
| CMake cannot find Boost or OpenSSL | Required version, installed package and CMake prefix | Set `CMAKE_PREFIX_PATH` or `OPENSSL_ROOT_DIR` to the installed dependency; use a separate build directory when changing providers |
| Asio/Beast socket types do not match | Compile definitions in verbose build output; earlier provider includes | Use one provider in all translation units; use the `arknet::arknet` interface target to propagate its definitions |
| Secure headers or OpenSSL symbols are unavailable | `ARKNET_ENABLE_SSL`, OpenSSL headers and link libraries | Enable TLS in CMake and use the exported interface target; for direct headers, define the macro and supply OpenSSL dependencies |
| TLS handshake rejects a certificate | Callback error, trust chain, certificate validity and requested DNS/IP identity | Load the intended CA and connect using a name or address present in the certificate's subject alternative names; check the system clock |
| A TLS server fails to start or reports `invalid_argument` | Error immediately after loading certificate and private key | Load a usable certificate and matching key before start; for mTLS, also configure the client CA and server verify mode |
| `async_send()` returns `false` with `no_buffer_space` | Configured limit, queued bytes and outstanding application requests | Pace the producer or reduce concurrency; retry only according to application policy after completions free capacity |
| `send()` returns zero with `in_progress` | Whether the call runs on the object's context worker | Observe the asynchronous completion with `async_send()`; do not wait for a future on that context's workers |
| Timer query returns a default result with `operation_not_supported` or `operation_aborted` | Different strand on the same context, or stopped context | Query on the object's executor or from an owner thread while runners remain active |
| Start, future or shutdown waits indefinitely with external IO | Active `run()` threads and work guard | Keep the host context running until objects finish stopping; follow the [shutdown order](../concepts/lifecycle.md#lifecycle-and-execution) |
| TCP receive callbacks split or combine application messages | Framing at both ends | Treat raw TCP as a byte stream, or use matching delimiter/custom framing or `use_dgram` at both ends; see [TCP usage](../features/tcp/index.md) |
| Documentation returns 404 locally | Preview URL and server output | Open `/arknet/` using the Nimbus development server below |

An error-free send completion confirms transport completion. To confirm peer application processing,
record and validate an application-protocol response.

<Aside type="danger" title="Error: TLS certificate verification failed">

Check the error from `bind_handshake`, the CA chain, validity dates and the requested DNS/IP identity.
Correct the certificate or connection name; keep peer verification enabled.

</Aside>

## Serve Documentation Locally

<Steps>

<Step title="Install documentation dependencies">

From the repository root, with Node.js 22.12 or newer:

```sh
cd docs/site
npm ci
```

</Step>

<Step title="Start the preview">

```sh
npm run dev -- --host 127.0.0.1 --port 8767
```

Open [http://127.0.0.1:8767/arknet/](http://127.0.0.1:8767/arknet/).
Choose another port if it is occupied. The language dropdown preserves the current page.

</Step>

<Step title="Verify the documentation">

```sh
npm run check
```

This checks content conversion, types, the static build, links and AI Markdown exports.

</Step>

</Steps>

## Include Reproduction Evidence

Include in issue reports:

- Source revision and the relevant minimal client/server or test case.
- OS, architecture, compiler and dependency versions.
- Asio provider, TLS configuration and CMake configure command.
- Owned or external IO, context/lane count and number of `run()` threads.
- Error category, value and message captured where the error occurred.
- For sends, payload size, framing, queue limit and completion byte count.
- Test logs or a benchmark JSON file, with the exact command used.

For validation evidence and reproduction commands, see [Testing](testing.md).
Repository certificates and keys are test-only; diagnose TLS using deployment trust and identity settings.
