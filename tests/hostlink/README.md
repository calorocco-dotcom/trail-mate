# HostLink management verification

Build with CMake and run CTest. Assertions stay enabled in all build types.

```sh
cmake -S tests/hostlink -B .codex-build/hostlink-codec/native
cmake --build .codex-build/hostlink-codec/native
ctest --test-dir .codex-build/hostlink-codec/native --output-on-failure
```

Tests cover framing parity, borrowed management envelopes, legacy/management
routing, queue-item size, transfer ownership/idempotency, snapshot consistency,
SD upload/commit control flow, and native IDF typed NVS compatibility.
The TMS scratch test compiles the production lease extracted from its source,
checks nested reuse and outer ownership, and injects PSRAM allocation failure.
Identity tests verify persistent generation changes, refusal to overwrite an
existing damaged account, and the unchanged 200-byte identity object. The native
proof executable uses the firmware's Ed25519 library and fixed test keys.
The IDF scheduling test compiles the production begin/step methods and verifies
peer/Geocaching fairness, bounded batches, and continuation without restarting a batch.

After the native build, run the cross-language check from the repository root:

```sh
node --test tests/site/author-native-interop.test.mjs
```

The dispatch/upload tests use fake storage and a deliberately noncryptographic
hash to exercise failures and state transitions. They do not prove SHA-256,
physical SD persistence, power-loss recovery, USB operation, or target stack
budgets. The NVS compatibility test checks key/type handling and error reporting,
not flash durability. Target builds and hardware verification remain separate.
The identity-init policy test compiles the actual production policy with simulated
key loading; it does not replace real NVS/crypto loading or hardware validation.
