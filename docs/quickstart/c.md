# Quick start — C / C++

## Prerequisites

```bash
# Ubuntu/Debian
sudo apt install cmake ninja-build gcc g++ libssl-dev

# Fedora/RHEL
sudo dnf install cmake ninja-build gcc gcc-c++ openssl-devel
```

---

## C (plain API)

```c
#include "baresdk.h"
#include <stdio.h>
#include <string.h>

static void on_event(const baresdk_event_t *ev, void *ud) {
    if (ev->type == BARESDK_EV_REG_STATE &&
        ev->u.reg.state == BARESDK_REG_REGISTERED) {
        puts("Registered!");
    }
    if (ev->type == BARESDK_EV_INCOMING_CALL) {
        printf("Incoming from %s\n", ev->u.incoming.from_uri);
        baresdk_call_answer(ev->u.incoming.call);
    }
}

int main(void) {
    baresdk_config_t cfg;
    baresdk_config_init(&cfg);
    cfg.log_level  = 1;
    cfg.event_cb   = on_event;

    baresdk_init(&cfg);

    baresdk_account_config_t acfg = {
        .uri      = "alice@pbx.example.com",
        .password = "secret",
        .transport = BARESDK_TRANSPORT_TLS,
    };
    baresdk_account_handle_t acct;
    baresdk_account_create(&acfg, &acct);
    baresdk_account_register(acct);

    /* ... wait for events via your own event loop ... */

    baresdk_account_destroy(acct);
    baresdk_shutdown();
    return 0;
}
```

**Build + compile (Linux):**
```bash
# 1. Build SDK
bash scripts/build-linux.sh

# 2. Compile — .so has OpenSSL/zlib/pthreads baked in; no extra -l flags needed
gcc main.c -I dist/linux/x86_64/include \
    dist/linux/x86_64/baresdk.so -o demo

# 3. Run
./demo
```

macOS / Windows / Android: run the corresponding build script — the shared lib is produced in the same step.

---

## C++ (header-only wrapper)

### One-command setup

```bash
bash bindings/cpp/build.sh
```

Builds the SDK if needed, then compiles all examples in `bindings/cpp/examples/`. The binary and `baresdk.so` are placed together so no `LD_LIBRARY_PATH` is needed.

### Manual compile

```bash
# After running bash scripts/build-linux.sh
g++ -std=c++17 main.cpp \
    -I dist/linux/x86_64/include -I bindings/cpp \
    dist/linux/x86_64/baresdk.so \
    -o demo

./demo
```

### Code

```cpp
#include "bindings/cpp/baresdk.hpp"
#include <iostream>

int main() {
    baresdk::SDK sdk;
    sdk.config().log_level = 1;

    sdk.on_event([](const baresdk_event_t& ev) {
        if (ev.type == BARESDK_EV_REG_STATE &&
            ev.u.reg.state == BARESDK_REG_REGISTERED)
            std::cout << "Registered!\n";

        if (ev.type == BARESDK_EV_INCOMING_CALL) {
            baresdk::Call c(ev.u.incoming.call);
            c.answer();
        }
    });

    auto acct = sdk.create_account("alice@pbx.example.com", "secret",
                                   BARESDK_TRANSPORT_TLS);
    acct.register_account();

    std::this_thread::sleep_for(std::chrono::seconds(30));
    return 0;
}
```

---

## See also
- Full C++ example: [bindings/cpp/examples/quickstart.cpp](../../bindings/cpp/examples/quickstart.cpp)
- API reference: [accounts](../api/accounts.md) · [calls](../api/calls.md) · [events](../api/events.md)
