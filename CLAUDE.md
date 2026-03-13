# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**doa4rfc** — Real-time Direction-of-Arrival estimation for RF Communication Protocols using Uniform Linear Antenna Arrays and USRP N210 hardware. C++20, Liquid-DSP, ZeroMQ, Python (MUSIC algorithm).

## Build Commands

```bash
# Configure (uses Clang, Debug mode)
cmake --preset ClangDebug

# Build all targets
cmake --build build/ClangDebug

# Build a specific target
cmake --build build/ClangDebug --target <target_name>
```

**Targets:** `doa4rfc` (main app), `sim_singlechannel`, `sim_multichannel`, `sim_music`, `sim_music_singlecarrier`, `music_gnuradio`

**Python setup** (for MUSIC algorithm):
```bash
cd music && python -m venv env && source env/bin/activate && pip install -e .
```

## Architecture

### Worker-based threading model

All major operations run as independent worker threads inheriting from `MultithreadWorker` (in `include/multithread_worker/`). Workers communicate via `ThreadSafeQueue<T>` — a mutex+condition-variable queue.

**Data flow:** USRP RX → `rx_worker` → `SampleBlockQueue_t` → `SyncWorker` → `FrameSampsQueue_t`/`FrameSymsQueue_t` → export workers → ZMQ socket → Python MUSIC GUI

### Key components

| Component | Location | Type | Purpose |
|-----------|----------|------|---------|
| Core types | `include/doa4rfc.h` | Header | `Sample_t`, `Samples_Ndim_t`, `liquid_conv` namespace |
| MultiSync | `include/multisync/` | Interface lib | Multi-channel frame sync with NCO phase correction |
| SyncTraits | `include/multisync/include/synctraits.h` | Header | Template traits for `ofdmframesync_iface` / `flexframesync_iface` |
| SyncWorker | `include/sync_worker/` | Interface lib | Template worker `SyncWorker<num_channels, sync_iface>` |
| MultithreadWorker | `include/multithread_worker/` | Static lib | Base class + `ThreadSafeQueue<T>` |
| ZMQ interface | `include/interfaces/zmq/` | Static lib | `ZmqRxWorker`, `ZmqTxWorker`, serialization |
| UHD interface | `include/interfaces/uhd/` | Static lib | USRP stream/rx/tx workers |
| Export worker | `include/export_worker/` | Static lib | CFR grouping, ZMQ + MATLAB export |
| Signal generator | `include/signal_generator/` | Static lib | Test signal generation for simulations |

### Liquid-DSP type conversions

`liquid_conv` namespace in `include/doa4rfc.h` bridges `std::complex<float>` ↔ `liquid_float_complex`:
- `Val(c)` — value copy (for function inputs)
- `Ref(c)` — reinterpret_cast pointer (for function outputs)
- `Ptr(p)` — pointer cast (for block operations)

### Synchronizer pattern

Synchronizer types are selected via template traits (`SyncTraits` specializations). The `SyncTraitsConcept` (C++20 concept) enforces the required interface. To add a new synchronizer, specialize `SyncTraits<>` in `synctraits.h`.

## External Dependencies

- **Liquid-DSP**: Git submodule in `external/liquid-dsp/` — frame sync, OFDM, NCO, channel simulation
- **matlabXport**: Fetched via CMake FetchContent from `https://github.com/F-L-X-S/matlabXport`
- **UHD**, **Boost**, **FFTW3**, **ZeroMQ**: System-installed

## Tests

Tests exist in `tests/` (Google Test) but are currently **commented out** in `CMakeLists.txt`.
