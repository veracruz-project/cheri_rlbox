#include "wasm-rt.h"

// Pull the helper header from the main repo for dynamic_check and scope_exit
#include "rlbox_helpers.hpp"
#include "rlbox_wasm2c_sandbox.hpp"
#include "wasm2c_details.hpp"

namespace rlbox {

void rlbox_wasm2c_sandbox::ensure_return_slot_size(size_t size)
{
  if (size > return_slot_size) {
    if (return_slot_size) {
      impl_free_in_sandbox(return_slot);
    }
    return_slot = impl_malloc_in_sandbox(size);
    detail::dynamic_check(
      return_slot != 0,
      "Error initializing return slot. Sandbox may be out of memory!");
    return_slot_size = size;
  }
}

template<typename T, typename T_Converted, typename... T_Args>
auto rlbox_wasm2c_sandbox::impl_invoke_with_func_ptr(T_Converted* func_ptr,
                                                     T_Args&&... params)
{
#ifdef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
  auto& thread_data = *get_rlbox_wasm2c_sandbox_thread_data();
#endif
  auto old_sandbox = thread_data.sandbox;
  thread_data.sandbox = this;
  auto on_exit =
    detail::make_scope_exit([&] { thread_data.sandbox = old_sandbox; });

  // WASM functions are mangled in the following manner
  // 1. All primitive types are left as is and follow an LP32 machine model
  // (as opposed to the possibly 64-bit application)
  // 2. All pointers are changed to u32 types
  // 3. Returned class are returned as an out parameter before the actual
  // function parameters
  // 4. All class parameters are passed as pointers (u32 types)
  // 5. The heap address is passed in as the first argument to the function
  //
  // RLBox accounts for the first 2 differences in T_Converted type, but we
  // need to handle the rest

  // Handle point 3
  using T_Ret = wasm2c_detail::return_argument<T_Converted>;
  if constexpr (std::is_class_v<T_Ret>) {
    using T_Conv1 = wasm2c_detail::change_return_type<T_Converted, void>;
    using T_Conv2 = wasm2c_detail::prepend_arg_type<T_Conv1, T_PointerType>;
    auto func_ptr_conv =
      reinterpret_cast<T_Conv2*>(reinterpret_cast<uintptr_t>(func_ptr));
    ensure_return_slot_size(sizeof(T_Ret));
    impl_invoke_with_func_ptr<T>(func_ptr_conv, return_slot, params...);

    auto ptr = reinterpret_cast<T_Ret*>(
      impl_get_unsandboxed_pointer<T_Ret*>(return_slot));
    T_Ret ret = *ptr;
    return ret;
  }

  // Handle point 4
  constexpr size_t alloc_length = [&] {
    if constexpr (sizeof...(params) > 0) {
      return ((std::is_class_v<T_Args> ? 1 : 0) + ...);
    } else {
      return 0;
    }
  }();

  // 0 arg functions create 0 length arrays which is not allowed
  T_PointerType allocations_buff[alloc_length == 0 ? 1 : alloc_length];
  T_PointerType* allocations = allocations_buff;

  auto serialize_class_arg =
    [&](auto arg) -> std::conditional_t<std::is_class_v<decltype(arg)>,
                                        T_PointerType,
                                        decltype(arg)> {
    using T_Arg = decltype(arg);
    if constexpr (std::is_class_v<T_Arg>) {
      auto slot = impl_malloc_in_sandbox(sizeof(T_Arg));
      auto ptr =
        reinterpret_cast<T_Arg*>(impl_get_unsandboxed_pointer<T_Arg*>(slot));
      *ptr = arg;
      allocations[0] = slot;
      allocations++;
      return slot;
    } else {
      return arg;
    }
  };

  // 0 arg functions don't use serialize
  RLBOX_WASM2C_UNUSED(serialize_class_arg);

  using T_ConvNoClass =
    wasm2c_detail::change_class_arg_types<T_Converted, T_PointerType>;

  // Handle Point 5
  using T_ConvHeap = wasm2c_detail::prepend_arg_type<T_ConvNoClass, void*>;

  // Function invocation
  auto func_ptr_conv =
    reinterpret_cast<T_ConvHeap*>(reinterpret_cast<uintptr_t>(func_ptr));

  using T_NoVoidRet =
    std::conditional_t<std::is_void_v<T_Ret>, uint32_t, T_Ret>;
  T_NoVoidRet ret;

  if constexpr (std::is_void_v<T_Ret>) {
    RLBOX_WASM2C_UNUSED(ret);
    func_ptr_conv(exec_env, serialize_class_arg(params)...);
  } else {
    ret = func_ptr_conv(exec_env, serialize_class_arg(params)...);
  }

  for (size_t i = 0; i < alloc_length; i++) {
    impl_free_in_sandbox(allocations_buff[i]);
  }

  if constexpr (!std::is_void_v<T_Ret>) {
    return ret;
  }
}

} // namespace rlbox