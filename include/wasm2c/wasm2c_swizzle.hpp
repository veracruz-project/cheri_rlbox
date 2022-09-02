#pragma once

#include "wasm-rt.h"

// Pull the helper header from the main repo for dynamic_check and scope_exit
#include "rlbox_helpers.hpp"
#include "rlbox_wasm2c_sandbox.hpp"
#include "wasm2c_details.hpp"

using rlbox::rlbox_wasm2c_sandbox;

namespace rlbox {

template<typename T_FormalRet, typename T_ActualRet>
auto rlbox_wasm2c_sandbox::serialize_to_sandbox(T_ActualRet arg)
{
  if constexpr (std::is_class_v<T_FormalRet>) {
    // structs returned as pointers into wasm memory/wasm stack
    auto ptr = reinterpret_cast<T_FormalRet*>(
      impl_get_unsandboxed_pointer<T_FormalRet*>(arg));
    T_FormalRet ret = *ptr;
    return ret;
  } else {
    return arg;
  }
}

inline bool rlbox_wasm2c_sandbox::impl_is_in_same_sandbox(const void* p1,
                                                          const void* p2)
{
  uintptr_t heap_base_mask = std::numeric_limits<uintptr_t>::max() &
                             ~(std::numeric_limits<T_PointerType>::max());
  return (reinterpret_cast<uintptr_t>(p1) & heap_base_mask) ==
         (reinterpret_cast<uintptr_t>(p2) & heap_base_mask);
}

inline bool rlbox_wasm2c_sandbox::impl_is_pointer_in_sandbox_memory(
  const void* p)
{
  size_t length = impl_get_total_memory();
  uintptr_t p_val = reinterpret_cast<uintptr_t>(p);
  return p_val >= heap_base && p_val < (heap_base + length);
}

inline bool rlbox_wasm2c_sandbox::impl_is_pointer_in_app_memory(const void* p)
{
  return !(rlbox_wasm2c_sandbox::impl_is_pointer_in_sandbox_memory(p));
}

inline size_t rlbox_wasm2c_sandbox::impl_get_total_memory()
{
  return sandbox_memory_info->size;
}

inline void* rlbox_wasm2c_sandbox::impl_get_memory_location() const
{
  return sandbox_memory_info->data;
}

template<typename T>
inline void* rlbox_wasm2c_sandbox::impl_get_unsandboxed_pointer(
  T_PointerType p) const
{
  if constexpr (std::is_function_v<std::remove_pointer_t<T>>) {
    RLBOX_ACQUIRE_UNIQUE_GUARD(lock, callback_mutex);
    auto found = slot_assignments.find(p);
    if (found != slot_assignments.end()) {
      auto ret = found->second;
      return const_cast<void*>(ret);
    } else {
      return nullptr;
    }
  } else {
    return reinterpret_cast<void*>(heap_base + p);
  }
}

template<typename T>
inline rlbox_wasm2c_sandbox::T_PointerType
rlbox_wasm2c_sandbox::impl_get_sandboxed_pointer(const void* p) const
{
  if constexpr (std::is_function_v<std::remove_pointer_t<T>>) {
    RLBOX_ACQUIRE_UNIQUE_GUARD(lock, callback_mutex);

    uint32_t slot_number = 0;
    auto found = internal_callbacks.find(p);
    if (found != internal_callbacks.end()) {
      slot_number = found->second;
    } else {

      auto func_type_idx = get_wasm2c_func_index(static_cast<T>(nullptr));
      slot_number = sandbox_info.add_wasm2c_callback(sandbox,
                                                     func_type_idx,
                                                     const_cast<void*>(p),
                                                     WASM_RT_INTERNAL_FUNCTION);
      internal_callbacks[p] = slot_number;
      slot_assignments[slot_number] = p;
    }
    return static_cast<T_PointerType>(slot_number);
  } else {
    if constexpr (sizeof(uintptr_t) == sizeof(uint32_t)) {
      return static_cast<T_PointerType>(reinterpret_cast<uintptr_t>(p) -
                                        heap_base);
    } else {
      return static_cast<T_PointerType>(reinterpret_cast<uintptr_t>(p));
    }
  }
}

template<typename T>
inline void* rlbox_wasm2c_sandbox::impl_get_unsandboxed_pointer_no_ctx(
  T_PointerType p,
  const void* example_unsandboxed_ptr,
  rlbox_wasm2c_sandbox* (*expensive_sandbox_finder)(
    const void* example_unsandboxed_ptr))
{
  // on 32-bit platforms we don't assume the heap is aligned
  if constexpr (sizeof(uintptr_t) == sizeof(uint32_t)) {
    auto sandbox = expensive_sandbox_finder(example_unsandboxed_ptr);
    return sandbox->impl_get_unsandboxed_pointer<T>(p);
  } else {
    if constexpr (std::is_function_v<std::remove_pointer_t<T>>) {
      // swizzling function pointers needs access to the function pointer tables
      // and thus cannot be done without context
      auto sandbox = expensive_sandbox_finder(example_unsandboxed_ptr);
      return sandbox->impl_get_unsandboxed_pointer<T>(p);
    } else {
      // grab the memory base from the example_unsandboxed_ptr
      uintptr_t heap_base_mask =
        std::numeric_limits<uintptr_t>::max() &
        ~(static_cast<uintptr_t>(std::numeric_limits<T_PointerType>::max()));
      uintptr_t computed_heap_base =
        reinterpret_cast<uintptr_t>(example_unsandboxed_ptr) & heap_base_mask;
      uintptr_t ret = computed_heap_base | p;
      return reinterpret_cast<void*>(ret);
    }
  }
}

template<typename T>
inline rlbox_wasm2c_sandbox::T_PointerType
rlbox_wasm2c_sandbox::impl_get_sandboxed_pointer_no_ctx(
  const void* p,
  const void* example_unsandboxed_ptr,
  rlbox_wasm2c_sandbox* (*expensive_sandbox_finder)(
    const void* example_unsandboxed_ptr))
{
  // on 32-bit platforms we don't assume the heap is aligned
  if constexpr (sizeof(uintptr_t) == sizeof(uint32_t)) {
    auto sandbox = expensive_sandbox_finder(example_unsandboxed_ptr);
    return sandbox->impl_get_sandboxed_pointer<T>(p);
  } else {
    if constexpr (std::is_function_v<std::remove_pointer_t<T>>) {
      // swizzling function pointers needs access to the function pointer tables
      // and thus cannot be done without context
      auto sandbox = expensive_sandbox_finder(example_unsandboxed_ptr);
      return sandbox->impl_get_sandboxed_pointer<T>(p);
    } else {
      // Just clear the memory base to leave the offset
      RLBOX_WASM2C_UNUSED(example_unsandboxed_ptr);
      uintptr_t ret = reinterpret_cast<uintptr_t>(p) &
                      std::numeric_limits<T_PointerType>::max();
      return static_cast<T_PointerType>(ret);
    }
  }
}
} // namespace rlbox