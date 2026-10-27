"""Rebuild the Akida Engine 2.19.3 + FlatBuffers 2.0.8 tree used by BrainBoard1500.

Usage:
    python patch_engine.py <akida_engine_dir> <flatbuffers_dir> <library_src_dir>

<akida_engine_dir> is the "akida/engine" folder of the akida 2.19.3 Python
wheel (or the output of "akida engine deploy"). <flatbuffers_dir> is a
checkout of https://github.com/google/flatbuffers at tag v2.0.8.

The script copies the pristine upstream sources into the library and applies
the small, documented set of changes listed in PATCHES.md. Every replacement
is asserted, so the script fails loudly if the upstream sources change.
"""
import glob
import os
import shutil
import sys

engine_dir, fb_dir, root = sys.argv[1], sys.argv[2], sys.argv[3]


def copy_glob(pattern, dest):
    os.makedirs(os.path.join(root, dest), exist_ok=True)
    for f in sorted(glob.glob(pattern)):
        shutil.copy(f, os.path.join(root, dest))


# 0. Fresh copy of the upstream sources (the AKD1000 bare-metal driver is not
#    needed on BB15 and is left out).
for d in ('akida', 'infra', 'akd1500', 'engine', 'akida_engine', 'flatbuffers'):
    shutil.rmtree(os.path.join(root, d), ignore_errors=True)
copy_glob(os.path.join(engine_dir, 'api', 'akida', '*.h'), 'akida')
copy_glob(os.path.join(engine_dir, 'api', 'infra', '*.h'), 'infra')
copy_glob(os.path.join(engine_dir, 'api', 'akd1500', '*.h'), 'akd1500')
copy_glob(os.path.join(engine_dir, 'inc', 'engine', '*.h'), 'engine')
copy_glob(os.path.join(engine_dir, 'src', '*'), 'akida_engine')
copy_glob(os.path.join(engine_dir, 'devices', 'akd1500', '*.cpp'), 'akida_engine')
copy_glob(os.path.join(fb_dir, 'include', 'flatbuffers', '*.h'), 'flatbuffers')
shutil.copy(os.path.join(fb_dir, 'LICENSE.txt'), os.path.join(root, 'flatbuffers', 'LICENSE.txt'))

HAS_TYPE = """  // BrainBoard1500 patch: the upstream implementation compares std::type_index
  // values obtained with typeid(). The Arduino mbed core compiles with
  // -fno-rtti, so the same mapping is expressed with std::is_same instead.
  // The accepted C++ type for every TensorType is unchanged.
  template<typename T>
  bool has_type() const {
    switch (this->type()) {
      case TensorType::int32:
        return std::is_same<T, int32_t>::value;
      case TensorType::int16:
        return std::is_same<T, int16_t>::value;
      case TensorType::uint16:
        return std::is_same<T, uint16_t>::value;
      case TensorType::float32:
        return std::is_same<T, float>::value;
      // NOTE: all uint types that fit in 8 bit are handled by the same C++
      // type, similarly for signed types. Only exception is bit, because 1
      // bit weights were historically handled in a int8_t.
      case TensorType::uint8:
      case TensorType::uint4:
      case TensorType::uint2:
        return std::is_same<T, uint8_t>::value;
      case TensorType::int8:
      case TensorType::int4:
      case TensorType::int2:
      case TensorType::bit:
        return std::is_same<T, int8_t>::value;
      default:
        break;
    }
    return std::is_same<T, void>::value;
  }

  /**
   * @brief Throw an exception if the Tensor doesn't have the specified
   * templated data type
   */
  template<typename T>
  void check_type() const {
    if (!has_type<T>()) {
      panic("Wrong requested type for a tensor of type %s",
            type_name(type()));
    }
  }

"""

def patch(rel, pairs):
    p = os.path.join(root, rel)
    s = open(p, encoding='utf8').read()
    for old, new in pairs:
        n = s.count(old)
        assert n >= 1, (rel, old[:80])
        s = s.replace(old, new)
    open(p, 'w', encoding='utf8', newline='\n').write(s)
    print('patched', rel)

TAG = '// BrainBoard1500 patch (RTTI-free type query), see PATCHES.md\n'

# 1. RTTI-free type queries on Tensor and its subclasses.
patch('akida/tensor.h', [(
    '  virtual bool operator==(const Tensor& ref) const = 0;',
    '  ' + TAG +
    '  virtual bool is_dense_tensor() const { return false; }\n'
    '  ' + TAG +
    '  virtual bool is_sparse_tensor() const { return false; }\n'
    '  ' + TAG +
    '  virtual bool is_dma_events_tensor() const { return false; }\n\n'
    '  virtual bool operator==(const Tensor& ref) const = 0;')])

def add_after_public(rel, cls_line, method):
    p = os.path.join(root, rel)
    s = open(p, encoding='utf8').read()
    i = s.index(cls_line)
    j = s.index(' public:\n', i) + len(' public:\n')
    s = s[:j] + '  ' + TAG + '  ' + method + '\n\n' + s[j:]
    open(p, 'w', encoding='utf8', newline='\n').write(s)
    print('patched', rel)

add_after_public('akida/dense.h', 'class AKIDASHAREDLIB_EXPORT Dense : public Tensor {',
                 'bool is_dense_tensor() const override { return true; }')
add_after_public('akida/sparse.h', 'class AKIDASHAREDLIB_EXPORT Sparse : public Tensor {',
                 'bool is_sparse_tensor() const override { return true; }')
add_after_public('akida_engine/dma_events.h', 'class DmaEvents : public Sparse {',
                 'bool is_dma_events_tensor() const override { return true; }')

# 2. dynamic_cast replacements.
patch('akida_engine/dense.cpp', [
    ('auto dense = dynamic_cast<const Dense*>(&ref);',
     'auto dense = ref.is_dense_tensor() ? static_cast<const Dense*>(&ref) : nullptr;'),
    ('auto sparse = dynamic_cast<const Sparse*>(&ref);',
     'auto sparse = ref.is_sparse_tensor() ? static_cast<const Sparse*>(&ref) : nullptr;')])
patch('akida_engine/sparse.cpp', [
    ('const auto* dense = dynamic_cast<const Dense*>(&ref);',
     'const auto* dense = ref.is_dense_tensor() ? static_cast<const Dense*>(&ref) : nullptr;')])
patch('akida_engine/dma_events_ops.cpp', [
    ('auto sparse = dynamic_cast<const Sparse*>(&inputs);',
     'auto sparse = inputs.is_sparse_tensor() ? static_cast<const Sparse*>(&inputs) : nullptr;'),
    ('dynamic_cast<const Dense&>(inputs)', 'static_cast<const Dense&>(inputs)')])
patch('akida_engine/hardware_device_impl.cpp', [
    ('dynamic_cast<const Sparse&>(*current_input)', 'static_cast<const Sparse&>(*current_input)'),
    ('const auto* dense = dynamic_cast<const Dense*>(input_to_queue);',
     'const auto* dense = input_to_queue->is_dense_tensor()\n'
     '                             ? static_cast<const Dense*>(input_to_queue)\n'
     '                             : nullptr;'),
    ('dynamic_cast<const Dense&>(*current_input)', 'static_cast<const Dense&>(*current_input)')])
patch('akida_engine/input_conversion.cpp', [
    ('return dynamic_cast<const DmaEvents*>(&input);',
     'return input.is_dma_events_tensor() ? static_cast<const DmaEvents*>(&input)\n'
     '                                    : nullptr;'),
    ('return dynamic_cast<const Dense*>(&input);',
     'return input.is_dense_tensor() ? static_cast<const Dense*>(&input) : nullptr;')])
patch('akida_engine/pico_conversion.cpp', [
    ('auto* dense_input = dynamic_cast<Dense*>(input.get());',
     'auto* dense_input = input->is_dense_tensor() ? static_cast<Dense*>(input.get())\n'
     '                                            : nullptr;')])

# 3. typeid on DMA input controllers.
patch('akida_engine/dma_engine.h', [
    ('  virtual ~Input() = default;\n  Engine engine;',
     '  virtual ~Input() = default;\n  ' + TAG + '  virtual bool is_hrc() const { return false; }\n  Engine engine;'),
    ('struct Hrc : public Input {\n  explicit Hrc(Engine e) : Input(e) {}',
     'struct Hrc : public Input {\n  explicit Hrc(Engine e) : Input(e) {}\n  bool is_hrc() const override { return true; }')])
patch('akida_engine/dma_desc_ops.cpp', [
    ('if (typeid(input_dma) == typeid(dma::Hrc)) {', 'if (input_dma.is_hrc()) {')])

# 4. std::optional -> nullable pointer.
patch('akida_engine/hardware_device_impl.h', [
    ('#include <optional>\n', ''),
    ('std::optional<PipelineState::slot> slot);',
     'const PipelineState::slot* slot);  // BrainBoard1500 patch: was std::optional')])
patch('akida_engine/hardware_device_impl.cpp', [
    ('#include <optional>\n', ''),
    ('std::optional<PipelineState::slot> slot) {',
     'const PipelineState::slot* slot) {  // BrainBoard1500 patch: was std::optional'),
    ('  if (!slot.has_value()) {\n    slot = pipeline_state_.reserve_job();\n  }\n  const auto& job_slot = slot.value();',
     '  PipelineState::slot reserved_slot{};\n  if (slot == nullptr) {\n    reserved_slot = pipeline_state_.reserve_job();\n    slot = &reserved_slot;\n  }\n  const auto& job_slot = *slot;'),
    ('return enqueue(address_in, input_size, label, {});', 'return enqueue(address_in, input_size, label, nullptr);'),
    ('return enqueue(address_in, input.buffer()->size(), label, job_slot);',
     'return enqueue(address_in, input.buffer()->size(), label, &job_slot);')])

# 5. C++17 library helpers.
patch('akida_engine/io_ops.h', [
    ('std::is_same_v<T2, int8_t>', 'std::is_same<T2, int8_t>::value'),
    ('std::is_same_v<T1, float>', 'std::is_same<T1, float>::value'),
    ('std::clamp<int32_t>(out_val, 0, 15)', 'std::min<int32_t>(std::max<int32_t>(out_val, 0), 15)'),
    ('std::clamp<int32_t>(out_val, -128, 127)', 'std::min<int32_t>(std::max<int32_t>(out_val, -128), 127)'),
    ('std::clamp<int32_t>(out_val, 0, 255)', 'std::min<int32_t>(std::max<int32_t>(out_val, 0), 255)')])

# 6. std::dynamic_pointer_cast -> checked std::static_pointer_cast.
patch('akida_engine/tensor.cpp', [
    ('return std::dynamic_pointer_cast<const Dense>(tensor);',
     'return (tensor && tensor->is_dense_tensor())\n'
     '             ? std::static_pointer_cast<const Dense>(tensor)\n'
     '             : nullptr;  // BrainBoard1500 patch: was dynamic_pointer_cast'),
    ('return std::dynamic_pointer_cast<const Sparse>(tensor);',
     'return (tensor && tensor->is_sparse_tensor())\n'
     '             ? std::static_pointer_cast<const Sparse>(tensor)\n'
     '             : nullptr;  // BrainBoard1500 patch: was dynamic_pointer_cast'),
    ('auto sparse = std::dynamic_pointer_cast<const Sparse>(tensor);',
     'auto sparse = (tensor && tensor->is_sparse_tensor())\n'
     '                    ? std::static_pointer_cast<const Sparse>(tensor)\n'
     '                    : nullptr;  // BrainBoard1500 patch: was dynamic_pointer_cast')])


# 7. AKIDA_VERSION is a per-file CMake definition upstream.
patch('akida_engine/version.cpp', [(
    '#include "akida/version.h"\n',
    '#include "akida/version.h"\n\n'
    '// BrainBoard1500 patch: the upstream CMake build injects AKIDA_VERSION as a\n'
    '// compile definition for this file only. The Arduino build system has no\n'
    '// per-file definitions, so the value from akida-engine.cmake is set here.\n'
    '#ifndef AKIDA_VERSION\n#define AKIDA_VERSION "2.19.3"\n#endif\n')])

# 8. typeid() in Tensor::has_type / check_type.
p = os.path.join(root, 'akida/tensor.h')
s = open(p, encoding='utf8').read()
start = s.index('  template<typename T>\n  bool has_type() const {')
end = s.index('  // BrainBoard1500 patch (RTTI-free type query)')
s = s[:start] + HAS_TYPE + s[end:]
assert s.count('#include <typeindex>\n') == 1
s = s.replace('#include <typeindex>\n', '#include <type_traits>\n')
open(p, 'w', encoding='utf8', newline='\n').write(s)
print('patched akida/tensor.h (has_type)')

# 9. FlatBuffers: the ARDUINO branches target AVR boards without an STL.
patch('flatbuffers/base.h', [
    ('#ifndef ARDUINO\n#include <cstdint>\n#endif',
     '// BrainBoard1500 patch: the ARDUINO special cases below target AVR boards without\n'
     '// a C++ standard library. mbed-based boards (Nicla Vision) ship a complete\n'
     '// libstdc++, so the standard headers are used there.\n'
     '#if !defined(ARDUINO) || !defined(__AVR__)\n#include <cstdint>\n#endif'),
    ('#if defined(ARDUINO) && !defined(ARDUINOSTL_M_H)',
     '#if defined(ARDUINO) && defined(__AVR__) && !defined(ARDUINOSTL_M_H)')])

# 10. The one throw in the engine: the Nicla Voice core builds with
#     -fno-exceptions, so it is reported through panic() like every other
#     engine error.
patch('akida/dense.h', [(
    '      throw std::runtime_error("Dense::transpose only supports 3D tensors.");',
    ('      // BrainBoard1500 patch: was a throw (no exceptions), see PATCHES.md\n'
     '      panic("Dense::transpose only supports 3D tensors.");'))])
print('all patches applied')
