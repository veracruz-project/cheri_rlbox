#pragma once

// Pull the helper header from the main repo for dynamic_check and scope_exit
#include "mswasm_details.hpp"
#include "rlbox_helpers.hpp"
#include "rlbox_mswasm_sandbox.hpp"

using rlbox::rlbox_mswasm_sandbox;

// IMPLEMENTS:
//    -  impl_get_unsandboxed_pointer
//    -  impl_get_sandboxed_pointer

namespace rlbox {

// TODO: implement once we have a partitioned allocator
// Currently this just returns true to pass the dynamic checks in upstream rlbox
inline bool rlbox_mswasm_sandbox::impl_is_in_same_sandbox(const void* p1,
                                                          const void* p2)
{
  return true;
}

// TODO: implement once we have a partitioned allocator
// Currently this just returns true to pass the dynamic checks in upstream rlbox
inline bool rlbox_mswasm_sandbox::impl_is_pointer_in_sandbox_memory(
  const void* p)
{
  return true;
}

// Note: removed impl_is_pointer_in_app_memory
// Note: removed impl_get_total_memory
// Note: removed impl_get_memory_location
// Can implement all of these if go with a partitioned-allocator design

// if its a function pointer and a callback, return converted pointer
// if its a function pointer and not a registered callback, return nullptr
// else cast and return
template<typename T>
inline void* rlbox_mswasm_sandbox::impl_get_unsandboxed_pointer(
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
    return reinterpret_cast<void*>(p);
  }
}

// get_sandboxed_pointer(p):
//    if func pointer and callback: return internal_callbacks[p]
//   if func pointer and not callback: (???)
//    else return p
template<typename T>
inline rlbox_mswasm_sandbox::T_PointerType
rlbox_mswasm_sandbox::impl_get_sandboxed_pointer(const void* p) const
{
  if constexpr (std::is_function_v<std::remove_pointer_t<T>>) {
    RLBOX_ACQUIRE_UNIQUE_GUARD(lock, callback_mutex);

    T_PointerType slot_number = 0;
    auto found = internal_callbacks.find(p);
    if (found != internal_callbacks.end()) {
      slot_number = found->second;
    } else {
      // TODO: finish this once we've implemented callbacks
      // auto func_type_idx = get_mswasm_func_index(static_cast<T>(nullptr));
      // slot_number = sandbox_info.add_mswasm_callback(sandbox,
      //                                                func_type_idx,
      //                                                const_cast<void*>(p),
      //                                                WASM_RT_INTERNAL_FUNCTION);
      // internal_callbacks[p] = slot_number;
      // slot_assignments[slot_number] = p;
    }
    return static_cast<T_PointerType>(slot_number);
  } else {
    return const_cast<T_PointerType>(p);
  }
}

// Note: removed impl_get_unsandboxed_pointer_no_ctx
// Note: removed impl_get_sandboxed_pointer_no_ctx
// I can implement these if I ever go with a partitioned-allocater design

} // namespace rlbox