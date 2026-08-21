# demofw00

`demofw00` is Fangwei's open5m factor demo module. It collects the fw tool
headers and the Shanghai-aware order synthesizer into one registered factor set.

## Layout

- `tools/base/`: reusable base operators, including statistics, ring buffers,
  OLS/PCA, trends, entropy, OU, and GARCH.
- `tools/market/`: market-domain logic, including OFI, big-event detection,
  chip structure, active-trader signals, order-id analyzers, and order
  synthesis.
- `factor_entry.*`: a runnable demo factor set that wires representative
  analyzers into 54 output fields.
- `meta_config.h`: factor-set name, factor count, and output column names.

## Naming Rules

- Tool files use snake_case names, for example `basic_stats.h`,
  `ring_buffer.hpp`, and `order_id_analyzer_trans.h`.
- Tool classes use PascalCase, for example `BasicStats`, `RingBuffer`,
  `OfiCalculator`, and `OrderSynthesizer`.
- Base operators use the namespace `factors::demofw00::tools::base`.
- Market logic uses the namespace `factors::demofw00::tools::market`.
- `base` must not depend on `market`; `market` may depend on `base`.
- Tool includes use the full project-relative path:
  `#include "factors/demofw00/tools/base/..."` or
  `#include "factors/demofw00/tools/market/..."`.
- Tool headers use `#pragma once`.

## Review Notes Fixed In This Copy

- Replaced old hard-coded include paths with `factors/demofw00/...`.
- Split copied helpers into `tools/base/` and `tools/market/`.
- Split helper namespaces into `tools::base` and `tools::market`.
- Renamed legacy files such as `baseutils.h`, `basicstats.h`, and
  `ringbuffer.hpp` to snake_case equivalents.
- Renamed `ring_buffer` to `RingBuffer`.
- Fixed missing standard headers in copied utilities.
- Fixed `RingBuffer` copy construction so non-empty buffers copy correctly.
- Propagated `OrderSynthesizer::OrderEvent::estimated` from internal state.
- Added `sdp_handler/quote_format_define.h` includes to method headers that
  reference quote structs.

## Build

This module uses C++17 because `ChipStructureAnalyzer` uses `std::optional`.

```bash
make build-factor CMAKE_CXX_STANDARD=17
./build/app_factor/main --version
```

The factor set name shown in configs is:

```text
demofw00
```
