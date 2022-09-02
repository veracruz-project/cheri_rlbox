#pragma once

#include "wasm-rt.h"

// Pull the helper header from the main repo for dynamic_check and scope_exit
#include "rlbox_helpers.hpp"
#include "rlbox_wasm2c_sandbox.hpp"
#include "wasm2c_details.hpp"

namespace rlbox {

#ifndef RLBOX_USE_STATIC_CALLS
void* rlbox_wasm2c_sandbox::symbol_lookup(std::string prefixed_name)
{
#  if defined(_WIN32)
  void* ret = (void*)GetProcAddress((HMODULE)library, prefixed_name.c_str());
#  else
  void* ret = dlsym(library, prefixed_name.c_str());
#  endif
  if (ret == nullptr) {
    // Some lookups such as globals are not exposed as shared library symbols
    uint32_t* heap_index_pointer =
      (uint32_t*)sandbox_info.lookup_wasm2c_nonfunc_export(
        sandbox, prefixed_name.c_str());
    if (heap_index_pointer != nullptr) {
      uint32_t heap_index = *heap_index_pointer;
      ret = &(reinterpret_cast<char*>(heap_base)[heap_index]);
    }
  }
  return ret;
}
#endif

// function takes a 32-bit value and returns the next power of 2
// return is a 64-bit value as large 32-bit values will return 2^32
uint64_t rlbox_wasm2c_sandbox::next_power_of_two(uint32_t value)
{
  uint64_t power = 1;
  while (power < value) {
    power *= 2;
  }
  return power;
}

#define WASM_PAGE_SIZE 65536
#define WASM_HEAP_MAX_ALLOWED_PAGES 65536
#define WASM_MAX_HEAP (static_cast<uint64_t>(1) << 32)
uint64_t rlbox_wasm2c_sandbox::rlbox_wasm2c_get_adjusted_heap_size(
  uint64_t heap_size)
{
  if (heap_size == 0) {
    return 0;
  }

  if (heap_size <= WASM_PAGE_SIZE) {
    return WASM_PAGE_SIZE;
  } else if (heap_size >= WASM_MAX_HEAP) {
    return WASM_MAX_HEAP;
  }

  return next_power_of_two(static_cast<uint32_t>(heap_size));
}

uint64_t rlbox_wasm2c_sandbox::rlbox_wasm2c_get_heap_page_count(
  uint64_t heap_size)
{
  const uint64_t pages = heap_size / WASM_PAGE_SIZE;
  return pages;
}
#undef WASM_MAX_HEAP
#undef WASM_HEAP_MAX_ALLOWED_PAGES
#undef WASM_PAGE_SIZE

#ifndef RLBOX_USE_STATIC_CALLS
void* rlbox_wasm2c_sandbox::impl_lookup_symbol(const char* func_name)
{
  std::string prefixed_name = "w2c_";
  prefixed_name += func_name;
  void* ret = symbol_lookup(prefixed_name);
  return ret;
}
#else

#  define rlbox_wasm2c_sandbox_lookup_symbol(func_name)                        \
    reinterpret_cast<void*>(&w2c_##func_name) /* NOLINT */

// adding a template so that we can use static_assert to fire only if this
// function is invoked
template<typename T = void>
void* rlbox_wasm2c_sandbox::impl_lookup_symbol(const char* func_name)
{
  constexpr bool fail = std::is_same_v<T, void>;
  static_assert(
    !fail,
    "The wasm2c_sandbox uses static calls and thus developers should add\n\n"
    "#define RLBOX_USE_STATIC_CALLS() rlbox_wasm2c_sandbox_lookup_symbol\n\n"
    "to their code, to ensure that static calls are handled correctly.");
  return nullptr;
}
#endif

inline rlbox_wasm2c_sandbox::T_PointerType
rlbox_wasm2c_sandbox::impl_malloc_in_sandbox(size_t size)
{
  if constexpr (sizeof(size) > sizeof(uint32_t)) {
    detail::dynamic_check(size <= std::numeric_limits<uint32_t>::max(),
                          "Attempting to malloc more than the heap size");
  }
  using T_Func = void*(size_t);
  using T_Converted = T_PointerType(uint32_t);
  T_PointerType ret = impl_invoke_with_func_ptr<T_Func, T_Converted>(
    reinterpret_cast<T_Converted*>(malloc_index), static_cast<uint32_t>(size));
  return ret;
}

inline void rlbox_wasm2c_sandbox::impl_free_in_sandbox(T_PointerType p)
{
  using T_Func = void(void*);
  using T_Converted = void(T_PointerType);
  impl_invoke_with_func_ptr<T_Func, T_Converted>(
    reinterpret_cast<T_Converted*>(free_index), p);
}

template<typename T_Ret, typename... T_Args>
uint32_t rlbox_wasm2c_sandbox::get_wasm2c_func_index(
  // dummy for template inference
  T_Ret (*)(T_Args...)) const
{
  // Class return types as promoted to args
  constexpr bool promoted = std::is_class_v<T_Ret>;

  // If return type is void, then there is no return type
  // But it is fine if we add it anyway as it as at the end of the array
  // and we pass in counts to lookup_wasm2c_func_index that would result in this
  // element not being accessed
  wasm_rt_type_t ret_param_types[] = {
    wasm2c_detail::convert_type_to_wasm_type<T_Args>::wasm2c_type...,
    wasm2c_detail::convert_type_to_wasm_type<T_Ret>::wasm2c_type
  };

  uint32_t param_count = 0;
  uint32_t ret_count = 0;

  if constexpr (promoted) {
    param_count = sizeof...(T_Args) + 1;
    ret_count = 0;
  } else {
    param_count = sizeof...(T_Args);
    ret_count = std::is_void_v<T_Ret> ? 0 : 1;
  }

  auto ret = sandbox_info.lookup_wasm2c_func_index(
    sandbox, param_count, ret_count, ret_param_types);
  return ret;
}

} // namespace rlbox