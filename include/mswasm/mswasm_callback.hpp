#pragma once

// Pull the helper header from the main repo for dynamic_check and scope_exit
#include "mswasm_details.hpp"
#include "rlbox_helpers.hpp"
#include "rlbox_mswasm_sandbox.hpp"

using rlbox::rlbox_mswasm_sandbox;

namespace rlbox {

// if arg is a struct/class, then replace with a pointer to that struct/class
// else return arg
template<typename T_FormalRet, typename T_ActualRet>
auto rlbox_mswasm_sandbox::serialize_to_sandbox(T_ActualRet arg)
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

template<uint32_t N, typename T_Ret, typename... T_Args>
// returns host representation of  the Wasm version of T_Ret
// typename mswasm_detail::convert_type_to_wasm_type<T_Ret>::type
inline rlbox_mswasm_sandbox::T_Guest<T_Ret>
rlbox_mswasm_sandbox::callback_interceptor_inner(void* /* vmContext */,
                                                 T_Guest<T_Args>... params)
{
#ifdef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
  auto& thread_data = *get_rlbox_mswasm_sandbox_thread_data();
#endif
  thread_data.last_callback_invoked = N;
  using T_Func = T_Ret (*)(T_Args...); // Host type signature?
  T_Func func;
  {
#ifndef RLBOX_SINGLE_THREADED_INVOCATIONS
    RLBOX_ACQUIRE_SHARED_GUARD(lock, thread_data.sandbox->callback_mutex);
#endif
    // lookup callback
    func = reinterpret_cast<T_Func>(thread_data.sandbox->callbacks[N]);
  }
  // Callbacks are invoked through function pointers, cannot use std::forward
  // as we don't have caller context for T_Args, which means they are all
  // effectively passed by value
  return func(thread_data.sandbox->serialize_to_sandbox<T_Args>(params)...);
}

// this is the trampoline invoked by the sandbox
// marks in the thread-local-storage that this is the current callback
// does the actuall callback lookup
// invokes callback[N](params)

// This is the Nth callback, with (host) type (T_Args -> T_Ret)
template<uint32_t N, typename T_Ret, typename... T_Args>
// returns host representation of  the Wasm version of T_Ret
// typename mswasm_detail::convert_type_to_wasm_type<T_Ret>::type
rlbox_mswasm_sandbox::T_Guest<T_Ret> rlbox_mswasm_sandbox::callback_interceptor(
  void* /* vmContext */,
  T_Guest<T_Args>... params)
{
  return callback_interceptor_inner<N, T_Ret, T_Args...>(params...);
}

// if  the callback returns a struct/class, then write it to the sandbox and
// return a pointer to it
// TODO: optimize this (and other callback/promotion stuff) using Morello magic
template<uint32_t N, typename T_Ret, typename... T_Args>
void rlbox_mswasm_sandbox::callback_interceptor_promoted(
  void* /* vmContext */,
  T_Guest<T_Ret> ret,
  T_Guest<T_Args>... params)
{
  auto ret_val = callback_interceptor_inner<N, T_Ret, T_Args...>(params...);

  // Copy the return value back
  auto ret_ptr = reinterpret_cast<T_Ret*>(
    thread_data.sandbox->template impl_get_unsandboxed_pointer<T_Ret*>(ret));
  *ret_ptr = ret_val;
}

template<typename T_Ret, typename... T_Args>
inline rlbox_mswasm_sandbox::T_PointerType
rlbox_mswasm_sandbox::impl_register_callback(void* key, void* callback)
{
  assert(false); // UNIMPLEMENTED

  // bool found = false;
  // uint32_t found_loc = 0;
  // void* chosen_interceptor = nullptr;

  // RLBOX_ACQUIRE_UNIQUE_GUARD(lock, callback_mutex);

  // // need a compile time for loop as we we need I to be a compile time value
  // // this is because we are setting the I'th callback interceptor
  // mswasm_detail::compile_time_for<MAX_CALLBACKS>([&](auto I) {
  //   constexpr auto i = I.value;
  //   if (!found && callbacks[i] == nullptr) {
  //     found = true;
  //     found_loc = i;

  //     if constexpr (std::is_class_v<T_Ret>) {
  //       chosen_interceptor = reinterpret_cast<void*>(
  //         callback_interceptor_promoted<i, T_Ret, T_Args...>);
  //     } else {
  //       chosen_interceptor =
  //         reinterpret_cast<void*>(callback_interceptor<i, T_Ret, T_Args...>);
  //     }
  //   }
  // });

  // detail::dynamic_check(
  //   found,
  //   "Could not find an empty slot in sandbox function table. This would "
  //   "happen if you have registered too many callbacks, or unsandboxed "
  //   "too many function pointers. You can file a bug if you want to "
  //   "increase the maximum allowed callbacks or unsandboxed functions "
  //   "pointers");

  // auto func_type_idx = get_mswasm_func_index<T_Ret, T_Args...>();
  // uint32_t slot_number = sandbox_info.add_mswasm_callback(
  //   sandbox, func_type_idx, chosen_interceptor, WASM_RT_EXTERNAL_FUNCTION);

  // callback_unique_keys[found_loc] = key;
  // callbacks[found_loc] = callback;
  // callback_slot_assignment[found_loc] = slot_number;
  // slot_assignments[slot_number] = callback;

  // return static_cast<T_PointerType>(slot_number);
}

std::pair<rlbox_mswasm_sandbox*, void*>
rlbox_mswasm_sandbox::impl_get_executed_callback_sandbox_and_key()
{
  assert(false); // UNIMPLEMENTED

  // #ifdef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
  //   auto& thread_data = *get_rlbox_mswasm_sandbox_thread_data();
  // #endif
  //   auto sandbox = thread_data.sandbox;
  //   auto callback_num = thread_data.last_callback_invoked;
  //   void* key = sandbox->callback_unique_keys[callback_num];
  //   return std::make_pair(sandbox, key);
}

template<typename T_Ret, typename... T_Args>
inline void rlbox_mswasm_sandbox::impl_unregister_callback(void* key)
{
  assert(false); // UNIMPLEMENTED

  // bool found = false;
  // uint32_t i = 0;
  // {
  //   RLBOX_ACQUIRE_UNIQUE_GUARD(lock, callback_mutex);
  //   for (; i < MAX_CALLBACKS; i++) {
  //     if (callback_unique_keys[i] == key) {
  //       sandbox_info.remove_mswasm_callback(sandbox,
  //                                           callback_slot_assignment[i]);
  //       callback_unique_keys[i] = nullptr;
  //       callbacks[i] = nullptr;
  //       callback_slot_assignment[i] = 0;
  //       found = true;
  //       break;
  //     }
  //   }
  // }

  // detail::dynamic_check(
  //   found, "Internal error: Could not find callback to unregister");

  // return;
}

} // namespace rlbox