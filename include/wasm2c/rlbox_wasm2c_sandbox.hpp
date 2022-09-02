#pragma once

#include "wasm-rt.h"
// Pull the helper header from the main repo for dynamic_check and scope_exit
#include "rlbox_helpers.hpp"
#include "rlbox_synchronize.hpp"
#include "wasm2c_details.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
// RLBox allows applications to provide a custom shared lock implementation
#ifndef RLBOX_USE_CUSTOM_SHARED_LOCK
#  include <shared_mutex>
#endif
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_WIN32)
// Ensure the min/max macro in the header doesn't collide with functions in
// std::
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

#define RLBOX_WASM2C_UNUSED(...) (void)__VA_ARGS__

#if defined(_WIN32)
using path_buf = const LPCWSTR;
#else
using path_buf = const char*;
#endif

namespace rlbox {
class rlbox_wasm2c_sandbox;
struct rlbox_wasm2c_sandbox_thread_data
{
  rlbox_wasm2c_sandbox* sandbox;
  uint32_t last_callback_invoked;
};

class rlbox_wasm2c_sandbox
{
public:
  using T_LongLongType = int64_t;
  using T_LongType = int32_t;
  using T_IntType = int32_t;
  using T_PointerType = uint32_t;
  using T_ShortType = int16_t;

private:
  void* sandbox = nullptr;
  wasm2c_sandbox_funcs_t sandbox_info;
#if !defined(_MSC_VER)
  __attribute__((weak))
#endif
  static std::once_flag wasm2c_runtime_initialized;
  wasm_rt_memory_t* sandbox_memory_info = nullptr;
#ifndef RLBOX_USE_STATIC_CALLS
  void* library = nullptr;
#endif
  uintptr_t heap_base;
  void* exec_env = 0;
  void* malloc_index = 0;
  void* free_index = 0;
  size_t return_slot_size = 0;
  T_PointerType return_slot = 0;

  static const size_t MAX_CALLBACKS = 128;
  mutable RLBOX_SHARED_LOCK(callback_mutex);
  void* callback_unique_keys[MAX_CALLBACKS]{ 0 };
  void* callbacks[MAX_CALLBACKS]{ 0 };
  uint32_t callback_slot_assignment[MAX_CALLBACKS]{ 0 };
  mutable std::map<const void*, uint32_t> internal_callbacks;
  mutable std::map<uint32_t, const void*> slot_assignments;

#ifndef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
  thread_local static inline rlbox_wasm2c_sandbox_thread_data thread_data{ 0,
                                                                           0 };
#endif

  template<typename T_FormalRet, typename T_ActualRet>
  inline auto serialize_to_sandbox(T_ActualRet arg);

  template<uint32_t N, typename T_Ret, typename... T_Args>
  static typename wasm2c_detail::convert_type_to_wasm_type<T_Ret>::type
  callback_interceptor(
    void* /* vmContext */,
    typename wasm2c_detail::convert_type_to_wasm_type<T_Args>::type... params);

  template<uint32_t N, typename T_Ret, typename... T_Args>
  static void callback_interceptor_promoted(
    void* /* vmContext */,
    typename wasm2c_detail::convert_type_to_wasm_type<T_Ret>::type ret,
    typename wasm2c_detail::convert_type_to_wasm_type<T_Args>::type... params);

  void ensure_return_slot_size(size_t size);

  template<typename T_Ret, typename... T_Args>
  inline uint32_t get_wasm2c_func_index(
    // dummy for template inference
    T_Ret (*)(T_Args...) = nullptr) const;

  static inline uint64_t next_power_of_two(uint32_t value);
  static uint64_t rlbox_wasm2c_get_adjusted_heap_size(uint64_t heap_size);
  static uint64_t rlbox_wasm2c_get_heap_page_count(uint64_t heap_size);

protected:
#ifndef RLBOX_USE_STATIC_CALLS
  inline void* symbol_lookup(std::string prefixed_name);
#endif
public:
#ifndef RLBOX_USE_STATIC_CALLS
  void* impl_lookup_symbol(const char* func_name);
#else
  template<typename T = void>
  void* impl_lookup_symbol(const char* func_name);
#endif

  inline bool impl_create_sandbox(
#ifndef RLBOX_USE_STATIC_CALLS
    path_buf wasm2c_module_path,
#endif
    bool infallible,
    uint64_t override_max_heap_size,
    const char* wasm_module_name);
  inline void impl_destroy_sandbox();

  template<typename T>
  inline void* impl_get_unsandboxed_pointer(T_PointerType p) const;

  template<typename T>
  inline T_PointerType impl_get_sandboxed_pointer(const void* p) const;

  template<typename T>
  static inline void* impl_get_unsandboxed_pointer_no_ctx(
    T_PointerType p,
    const void* example_unsandboxed_ptr,
    rlbox_wasm2c_sandbox* (*expensive_sandbox_finder)(
      const void* example_unsandboxed_ptr));

  template<typename T>
  static inline T_PointerType impl_get_sandboxed_pointer_no_ctx(
    const void* p,
    const void* example_unsandboxed_ptr,
    rlbox_wasm2c_sandbox* (*expensive_sandbox_finder)(
      const void* example_unsandboxed_ptr));

  static inline bool impl_is_in_same_sandbox(const void* p1, const void* p2);
  inline bool impl_is_pointer_in_sandbox_memory(const void* p);

  inline bool impl_is_pointer_in_app_memory(const void* p);
  inline size_t impl_get_total_memory();
  inline void* impl_get_memory_location() const;

  template<typename T, typename T_Converted, typename... T_Args>
  auto impl_invoke_with_func_ptr(T_Converted* func_ptr, T_Args&&... params);

  inline T_PointerType impl_malloc_in_sandbox(size_t size);
  inline void impl_free_in_sandbox(T_PointerType p);

  template<typename T_Ret, typename... T_Args>
  inline T_PointerType impl_register_callback(void* key, void* callback);

  static inline std::pair<rlbox_wasm2c_sandbox*, void*>
  impl_get_executed_callback_sandbox_and_key();

  template<typename T_Ret, typename... T_Args>
  inline void impl_unregister_callback(void* key);
};
} // namespace rlbox