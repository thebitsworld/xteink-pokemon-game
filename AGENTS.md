# Agent notes

Changes that have to be repeated in several files. Follow the linked checklist
before calling any of them done.

- **New touch device or touch controller**: every edit is listed in
  [docs/adding-a-touch-controller.md](docs/adding-a-touch-controller.md). A missed
  step can build cleanly and ship a touchscreen that does nothing.
- **New device (any)**: see "Adding a new device" in [README.md](README.md).
- **Host tests**: [docs/testing.md](docs/testing.md). Run the suites for every
  library you touched, plus the suites that compile `BoardConfig.h` when you edit it.
