#pragma once

// Pull the helper header from the main repo for dynamic_check and scope_exit
#include "mswasm_details.hpp"
#include "rlbox_helpers.hpp"
#include "rlbox_synchronize.hpp"

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

#define RLBOX_mswasm_UNUSED(...) (void)__VA_ARGS__

#if defined(_WIN32)
using path_buf = const LPCWSTR;
#else
using path_buf = const char*;
#endif

namespace rlbox {
class rlbox_mswasm_sandbox;
struct rlbox_mswasm_sandbox_thread_data
{
  rlbox_mswasm_sandbox* sandbox;
  uint32_t last_callback_invoked;
};

// TODO: deduplicate this with sandbox-emitted header (in rlbox_support.h) for
// mswasm_sandbox_funcs_t?
typedef void* (*create_mswasm_sandbox_t)(uint32_t argc, void* argv);
typedef void (*destroy_mswasm_sandbox_t)(void* ctx);

typedef struct mswasm_sandbox_funcs_t
{
  create_mswasm_sandbox_t create_mswasm_sandbox;
  destroy_mswasm_sandbox_t destroy_mswasm_sandbox;
  // add_callback_t add_mswasm_callback;
  // remove_callback_t remove_mswasm_callback;
} mswasm_sandbox_funcs_t;

class rlbox_mswasm_sandbox
{
  //////////////////// Backend-specific types //////////////////////////////
public:
  using T_LongLongType = int64_t;
  using T_LongType = int32_t;
  using T_IntType = int32_t;
  using T_PointerType = void*;
  using T_ShortType = int16_t;

  // Host representation of Wasm version of T
  // i.e., host type post-wasm information loss
  // Places where this is different from T:
  // T = enum ==> T_Guest = i64
  // T = pointer ==> T_Guest = void*
  // T = class/struct ==> T_Guest = void*
  template<typename T>
  using T_Guest = typename mswasm_detail::convert_type_to_wasm_type<T>::type;

  ////////////////////// Internal backend-specific state //////////////////////
private:
  void* sandbox = nullptr;
  mswasm_sandbox_funcs_t sandbox_info;
#if !defined(_MSC_VER)
  __attribute__((weak))
#endif
  static std::once_flag mswasm_runtime_initialized;
#ifndef RLBOX_USE_STATIC_CALLS
  void* library = nullptr;
#endif
  // uintptr_t heap_base;
  //  TODO: exec_env == VmCtx???
  void* exec_env = 0;
  // void* malloc_index = 0;
  // void* free_index = 0;
  size_t return_slot_size = 0;
  T_PointerType return_slot = 0;

  // callback state
  static const size_t MAX_CALLBACKS = 128;
  mutable RLBOX_SHARED_LOCK(callback_mutex);
  // void* callback_unique_keys[MAX_CALLBACKS]{ 0 };
  // Wasm callback index -> host function pointer
  void* callbacks[MAX_CALLBACKS]{ 0 };
  // uint32_t callback_slot_assignment[MAX_CALLBACKS]{ 0 };
  // TODO: may be able to remove both of these entirely since we have same
  // pointer representation in guest and host?
  mutable std::map<const void*, T_PointerType> internal_callbacks;
  mutable std::map<T_PointerType, const void*> slot_assignments;

#ifndef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
  thread_local static inline rlbox_mswasm_sandbox_thread_data thread_data{ 0,
                                                                           0 };
#endif

  //////////////// Internal backend-specific helpers  ////////////////

  // if arg is a struct/class, return pointer to that struct, else return arg
  // used when marshaling arguments for callbacks
  template<typename T_FormalRet, typename T_ActualRet>
  inline auto serialize_to_sandbox(T_ActualRet arg);

  // Trampoline invoked by sandbox when it wants to invoke a sandbox
  template<uint32_t N, typename T_Ret, typename... T_Args>
  inline static T_Guest<T_Ret> callback_interceptor_inner(
    void* /* vmContext */,
    T_Guest<T_Args>... params);

  // Trampoline invoked by sandbox when it wants to invoke a sandbox
  template<uint32_t N, typename T_Ret, typename... T_Args>
  static T_Guest<T_Ret> callback_interceptor(void* /* vmContext */,
                                             T_Guest<T_Args>... params);

  template<uint32_t N, typename T_Ret, typename... T_Args>
  static void callback_interceptor_promoted(void* /* vmContext */,
                                            T_Guest<T_Ret> ret,
                                            T_Guest<T_Args>... params);

  // Each sandbox has an allocated region to pass return values back to the host
  // application this function checks that the return slot is at least `size`
  // bytes, and if it doesn't it expands the allocation
  void ensure_return_slot_size(size_t size);
  // mswasm_sandbox_funcs_t* get_inner_sandbox_funcs( const char*
  // wasm_module_name);

  // template<typename T_Ret, typename... T_Args>
  // inline uint32_t get_mswasm_func_index(
  //   // dummy for template inference
  //   T_Ret (*)(T_Args...) = nullptr) const;

protected:
#ifndef RLBOX_USE_STATIC_CALLS
  inline void* symbol_lookup(std::string prefixed_name);
#endif

  ///////////// Frontend-facing public API   /////////////
public:
//===== symbols
#ifndef RLBOX_USE_STATIC_CALLS
  void* impl_lookup_symbol(const char* func_name);
#else
  template<typename T = void>
  void* impl_lookup_symbol(const char* func_name);
#endif

  //===== setup/teardown
  inline bool impl_create_sandbox(
#ifndef RLBOX_USE_STATIC_CALLS
    path_buf mswasm_module_path,
#endif
    bool infallible,
    uint32_t sbox_argc,
    void* sbox_argv,
    const char* wasm_module_name);
  inline void impl_destroy_sandbox();

  //===== swizzling
  template<typename T>
  inline void* impl_get_unsandboxed_pointer(T_PointerType p) const;

  template<typename T>
  inline T_PointerType impl_get_sandboxed_pointer(const void* p) const;

  static inline bool impl_is_in_same_sandbox(const void* p1, const void* p2);
  inline bool impl_is_pointer_in_sandbox_memory(const void* p);

  //===== function invocation
  template<typename T, typename T_Converted, typename... T_Args>
  auto impl_invoke_with_func_ptr(T_Converted* func_ptr, T_Args&&... params);

  inline T_PointerType impl_malloc_in_sandbox(size_t size);
  inline void impl_free_in_sandbox(T_PointerType p);

  //===== callbacks
  template<typename T_Ret, typename... T_Args>
  inline T_PointerType impl_register_callback(void* key, void* callback);

  static inline std::pair<rlbox_mswasm_sandbox*, void*>
  impl_get_executed_callback_sandbox_and_key();

  template<typename T_Ret, typename... T_Args>
  inline void impl_unregister_callback(void* key);
};
} // namespace rlbox