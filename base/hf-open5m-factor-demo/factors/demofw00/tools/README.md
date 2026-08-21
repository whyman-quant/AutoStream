# Tools

Reusable code is divided into two dependency layers.

## 1. Base Operators

Directory: `base/`

Namespace: `factors::demofw00::tools::base`

This layer contains domain-independent streaming and numerical operators:

- basic and window statistics
- ring buffer
- online OLS and PCA
- trend and trend fitting
- entropy
- OU and GARCH helpers

The base layer must not include or depend on files from `market/`.

## 2. Market Logic

Directory: `market/`

Namespace: `factors::demofw00::tools::market`

This layer contains market-data and microstructure logic:

- OFI calculation and state analysis
- big-event detection
- chip-structure analysis
- active-trader signals
- order-id analysis
- Shanghai-aware order synthesis

The venue-aware mother-order builder, field units, lifecycle semantics, and
real-data replay commands are documented in
[`market/ORDER_BUILDER_V2.md`](market/ORDER_BUILDER_V2.md).

Market logic may use operators from `base/`.
