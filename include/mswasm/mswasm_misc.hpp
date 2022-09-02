#pragma once

// Pull the helper header from the main repo for dynamic_check and scope_exit
#include "mswasm_details.hpp"
#include "rlbox_helpers.hpp"
#include "rlbox_mswasm_sandbox.hpp"

namespace rlbox {

#ifndef RLBOX_USE_STATIC_CALLS
void* rlbox_mswasm_sandbox::symbol_lookup(std::string prefixed_name)
{
  void* ret = dlsym(library, prefixed_name.c_str());
  // if (ret == nullptr) {
  //   // Some lookups such as globals are not exposed as shared library symbols
  //   uint32_t* heap_index_pointer =
  //     (uint32_t*)sandbox_info.lookup_mswasm_nonfunc_export(
  //       sandbox, prefixed_name.c_str());
  //   if (heap_index_pointer != nullptr) {
  //     uint32_t heap_index = *heap_index_pointer;
  //     ret = &(reinterpret_cast<char*>(heap_base)[heap_index]);
  //   }
  // }
  return ret;
}
#endif

#ifndef RLBOX_USE_STATIC_CALLS
void* rlbox_mswasm_sandbox::impl_lookup_symbol(const char* func_name)
{
  // Can add a symbol name prefix here if need be
  void* ret = symbol_lookup(func_name);
  return ret;
}
#else

#  define rlbox_mswasm_sandbox_lookup_symbol(func_name)                        \
    reinterpret_cast<void*>(&w2c_##func_name) /* NOLINT */

// adding a template so that we can use static_assert to fire only if this
// function is invoked
template<typename T = void>
void* rlbox_mswasm_sandbox::impl_lookup_symbol(const char* func_name)
{
  assert(false); // UNIMPLEMENTED

  constexpr bool fail = std::is_same_v<T, void>;
  static_assert(
    !fail,
    "The mswasm_sandbox uses static calls and thus developers should add\n\n"
    "#define RLBOX_USE_STATIC_CALLS() rlbox_mswasm_sandbox_lookup_symbol\n\n"
    "to their code, to ensure that static calls are handled correctly.");
  return nullptr;
}
#endif

inline rlbox_mswasm_sandbox::T_PointerType
rlbox_mswasm_sandbox::impl_malloc_in_sandbox(size_t size)
{
  // since we are using Cheri object-level sandboxing, mallocing in sandbox is
  // just a regular malloc  + convert to tainted
  T_PointerType ret = malloc(size);
  return ret;
}

inline void rlbox_mswasm_sandbox::impl_free_in_sandbox(T_PointerType p)
{
  // since we are using Cheri object-level sandboxing, freeing in sandbox is
  // just a regular free
  free(p);
}

// template<typename T_Ret, typename... T_Args>
// uint32_t rlbox_mswasm_sandbox::get_mswasm_func_index(
//   // dummy for template inference
//   T_Ret (*)(T_Args...)) const
// {
//   // Class return types as promoted to args
//   constexpr bool promoted = std::is_class_v<T_Ret>;

//   // If return type is void, then there is no return type
//   // But it is fine if we add it anyway as it as at the end of the array
//   // and we pass in counts to lookup_mswasm_func_index that would result in
//   this
//   // element not being accessed
//   wasm_rt_type_t ret_param_types[] = {
//     mswasm_detail::convert_type_to_wasm_type<T_Args>::mswasm_type...,
//     mswasm_detail::convert_type_to_wasm_type<T_Ret>::mswasm_type
//   };

//   uint32_t param_count = 0;
//   uint32_t ret_count = 0;

//   if constexpr (promoted) {
//     param_count = sizeof...(T_Args) + 1;
//     ret_count = 0;
//   } else {
//     param_count = sizeof...(T_Args);
//     ret_count = std::is_void_v<T_Ret> ? 0 : 1;
//   }

//   auto ret = sandbox_info.lookup_mswasm_func_index(
//     sandbox, param_count, ret_count, ret_param_types);
//   return ret;
//}

} // namespace rlbox