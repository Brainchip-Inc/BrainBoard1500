# Akida Engine 2.19.3 – changes for the Arduino mbed core

The Akida Engine is shipped in source inside the `akida` 2.19.3 Python
package (`akida/engine`, also produced by `akida engine deploy`). It targets
C++17 with RTTI. The Arduino mbed core for the Nicla boards compiles with
`-std=gnu++14 -fno-rtti`, and the Arduino IDE cannot change these flags for a
single library. `tools/patch_engine.py` copies the pristine sources into
`src/` and applies the small set of changes below; every replacement is
asserted, so the script fails if the upstream sources change.

```
python tools/patch_engine.py <akida>/engine <flatbuffers-v2.0.8> src
```

| File | Change | Reason |
|---|---|---|
| `akida/tensor.h` | `has_type()` / `check_type()` use `std::is_same` instead of `typeid` / `std::type_index` | no RTTI |
| `akida/tensor.h`, `akida/dense.h`, `akida/sparse.h`, `akida_engine/dma_events.h` | virtual `is_dense_tensor()`, `is_sparse_tensor()`, `is_dma_events_tensor()` | replace `dynamic_cast` |
| `akida_engine/dense.cpp`, `sparse.cpp`, `dma_events_ops.cpp`, `hardware_device_impl.cpp`, `input_conversion.cpp`, `pico_conversion.cpp`, `tensor.cpp` | `dynamic_cast` / `dynamic_pointer_cast` → checked `static_cast` / `static_pointer_cast` | no RTTI |
| `akida_engine/dma_engine.h`, `dma_desc_ops.cpp` | virtual `is_hrc()` on the DMA input controller instead of `typeid` | no RTTI |
| `akida_engine/hardware_device_impl.*` | `std::optional<PipelineState::slot>` → nullable pointer | C++14 library |
| `akida_engine/io_ops.h` | `std::is_same_v` → `std::is_same<>::value`, `std::clamp` → `std::min`/`std::max` | C++14 library |
| `akida_engine/version.cpp` | defines `AKIDA_VERSION "2.19.3"` (a per-file CMake definition upstream) | no per-file defines |
| `flatbuffers/base.h` | the `ARDUINO` special cases (no STL) only apply to AVR | mbed has a full libstdc++ |

`if constexpr` and inline variables remain; GCC accepts them in gnu++14 mode
as extensions. The AKD1000 bare-metal driver (`devices/akd1000`) is not
copied because the BB15 only carries an AKD1500.

Behaviour is unchanged: every replaced cast checks the same type
relationship that the original `dynamic_cast` checked.
