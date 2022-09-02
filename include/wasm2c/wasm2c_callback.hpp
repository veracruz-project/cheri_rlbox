#pragma once

#include "wasm-rt.h"

// Pull the helper header from the main repo for dynamic_check and scope_exit
#include "rlbox_helpers.hpp"
#include "rlbox_wasm2c_sandbox.hpp"
#include "wasm2c_details.hpp"

using rlbox::rlbox_wasm2c_sandbox;

namespace rlbox {

template<uint32_t N, typename T_Ret, typename... T_Args>
typename wasm2c_detail::convert_type_to_wasm_type<T_Ret>::type
rlbox_wasm2c_sandbox::callback_interceptor(
  void* /* vmContext */,
  typename wasm2c_detail::convert_type_to_wasm_type<T_Args>::type... params)
{
#ifdef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
  auto& thread_data = *get_rlbox_wasm2c_sandbox_thread_data();
#endif
  thread_data.last_callback_invoked = N;
  using T_Func = T_Ret (*)(T_Args...);
  T_Func func;
  {
#ifndef RLBOX_SINGLE_THREADED_INVOCATIONS
    RLBOX_ACQUIRE_SHARED_GUARD(lock, thread_data.sandbox->callback_mutex);
#endif
    func = reinterpret_cast<T_Func>(thread_data.sandbox->callbacks[N]);
  }
  // Callbacks are invoked through function pointers, cannot use std::forward
  // as we don't have caller context for T_Args, which means they are all
  // effectively passed by value
  return func(thread_data.sandbox->serialize_to_sandbox<T_Args>(params)...);
}

template<uint32_t N, typename T_Ret, typename... T_Args>
void rlbox_wasm2c_sandbox::callback_interceptor_promoted(
  void* /* vmContext */,
  typename wasm2c_detail::convert_type_to_wasm_type<T_Ret>::type ret,
  typename wasm2c_detail::convert_type_to_wasm_type<T_Args>::type... params)
{
#ifdef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
  auto& thread_data = *get_rlbox_wasm2c_sandbox_thread_data();
#endif
  thread_data.last_callback_invoked = N;
  using T_Func = T_Ret (*)(T_Args...);
  T_Func func;
  {
#ifndef RLBOX_SINGLE_THREADED_INVOCATIONS
    RLBOX_ACQUIRE_SHARED_GUARD(lock, thread_data.sandbox->callback_mutex);
#endif
    func = reinterpret_cast<T_Func>(thread_data.sandbox->callbacks[N]);
  }
  // Callbacks are invoked through function pointers, cannot use std::forward
  // as we don't have caller context for T_Args, which means they are all
  // effectively passed by value
  auto ret_val =
    func(thread_data.sandbox->serialize_to_sandbox<T_Args>(params)...);
  // Copy the return value back
  auto ret_ptr = reinterpret_cast<T_Ret*>(
    thread_data.sandbox->template impl_get_unsandboxed_pointer<T_Ret*>(ret));
  *ret_ptr = ret_val;
}

template<typename T_Ret, typename... T_Args>
inline rlbox_wasm2c_sandbox::T_PointerType
rlbox_wasm2c_sandbox::impl_register_callback(void* key, void* callback)
{
  bool found = false;
  uint32_t found_loc = 0;
  void* chosen_interceptor = nullptr;

  RLBOX_ACQUIRE_UNIQUE_GUARD(lock, callback_mutex);

  // need a compile time for loop as we we need I to be a compile time value
  // this is because we are setting the I'th callback ineterceptor
  wasm2c_detail::compile_time_for<MAX_CALLBACKS>([&](auto I) {
    constexpr auto i = I.value;
    if (!found && callbacks[i] == nullptr) {
      found = true;
      found_loc = i;

      if constexpr (std::is_class_v<T_Ret>) {
        chosen_interceptor = reinterpret_cast<void*>(
          callback_interceptor_promoted<i, T_Ret, T_Args...>);
      } else {
        chosen_interceptor =
          reinterpret_cast<void*>(callback_interceptor<i, T_Ret, T_Args...>);
      }
    }
  });

  detail::dynamic_check(
    found,
    "Could not find an empty slot in sandbox function table. This would "
    "happen if you have registered too many callbacks, or unsandboxed "
    "too many function pointers. You can file a bug if you want to "
    "increase the maximum allowed callbacks or unsadnboxed functions "
    "pointers");

  auto func_type_idx = get_wasm2c_func_index<T_Ret, T_Args...>();
  uint32_t slot_number = sandbox_info.add_wasm2c_callback(
    sandbox, func_type_idx, chosen_interceptor, WASM_RT_EXTERNAL_FUNCTION);

  callback_unique_keys[found_loc] = key;
  callbacks[found_loc] = callback;
  callback_slot_assignment[found_loc] = slot_number;
  slot_assignments[slot_number] = callback;

  return static_cast<T_PointerType>(slot_number);
}

std::pair<rlbox_wasm2c_sandbox*, void*>
rlbox_wasm2c_sandbox::impl_get_executed_callback_sandbox_and_key()
{
#ifdef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
  auto& thread_data = *get_rlbox_wasm2c_sandbox_thread_data();
#endif
  auto sandbox = thread_data.sandbox;
  auto callback_num = thread_data.last_callback_invoked;
  void* key = sandbox->callback_unique_keys[callback_num];
  return std::make_pair(sandbox, key);
}

template<typename T_Ret, typename... T_Args>
inline void rlbox_wasm2c_sandbox::impl_unregister_callback(void* key)
{
  bool found = false;
  uint32_t i = 0;
  {
    RLBOX_ACQUIRE_UNIQUE_GUARD(lock, callback_mutex);
    for (; i < MAX_CALLBACKS; i++) {
      if (callback_unique_keys[i] == key) {
        sandbox_info.remove_wasm2c_callback(sandbox,
                                            callback_slot_assignment[i]);
        callback_unique_keys[i] = nullptr;
        callbacks[i] = nullptr;
        callback_slot_assignment[i] = 0;
        found = true;
        break;
      }
    }
  }

  detail::dynamic_check(
    found, "Internal error: Could not find callback to unregister");

  return;
}

} // namespace rlbox