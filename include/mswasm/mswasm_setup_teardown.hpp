#pragma once

// Pull the helper header from the main repo for dynamic_check and scope_exit
#include "mswasm_details.hpp"
#include "rlbox_helpers.hpp"
#include "rlbox_mswasm_sandbox.hpp"

namespace rlbox {
#define FALLIBLE_DYNAMIC_CHECK(infallible, cond, msg)                          \
  if (infallible) {                                                            \
    detail::dynamic_check(cond, msg);                                          \
  } else if (!(cond)) {                                                        \
    impl_destroy_sandbox();                                                    \
    return false;                                                              \
  }

// Note - cut out Windows bits because Cheri does not support Windows.

/**
 * @brief creates the Wasm sandbox from the given shared library
 *
 * @param mswasm_module_path path to shared library compiled with mswasm. This
 * param is not specified if you are creating a statically linked sandbox.
 * @param infallible if set to true, the sandbox aborts on failure. If false,
 * the sandbox returns creation status as a return value
 * @param override_max_heap_size optional override of the maximum size of the
 * wasm heap allowed for this sandbox instance. When the value is zero, platform
 * defaults are used. Non-zero values are rounded to max(64k, next power of 2).
 * @param wasm_module_name optional module name used when compiling with mswasm
 * @return true when sandbox is successfully created
 * @return false when infallible if set to false and sandbox was not
 * successfully created. If infallible is set to true, this function will never
 * return false.
 */
bool rlbox_mswasm_sandbox::impl_create_sandbox(
#ifndef RLBOX_USE_STATIC_CALLS
  path_buf mswasm_module_path,
#endif
  bool infallible = true,
  uint32_t sbox_argc = 0,
  void* sbox_argv = nullptr,
  const char* wasm_module_name = "")
{
  FALLIBLE_DYNAMIC_CHECK(
    infallible, sandbox == nullptr, "Sandbox already initialized");

// 1) Open the dynamic library
#ifndef RLBOX_USE_STATIC_CALLS
  library = dlopen(mswasm_module_path, RTLD_LAZY);
#endif
  if (!library) {
    std::string error_msg = "Could not load mswasm dynamic library: ";

    error_msg += dlerror();
    FALLIBLE_DYNAMIC_CHECK(infallible, false, error_msg.c_str());
    return library;
  }
  // 2) Summon the info func
#ifndef RLBOX_USE_STATIC_CALLS
  std::string info_func_name = wasm_module_name;
  info_func_name += "get_mswasm_sandbox_info";
  auto get_info_func = reinterpret_cast<mswasm_sandbox_funcs_t (*)()>(
    symbol_lookup(info_func_name));
#else
  // only permitted if there is no custom module name
  std::string wasm_module_name_str = wasm_module_name;
  FALLIBLE_DYNAMIC_CHECK(
    infallible,
    wasm_module_name_str.empty(),
    "Static calls not supported with non empty module names");
  auto get_info_func =
    reinterpret_cast<mswasm_sandbox_funcs_t (*)()>(get_mswasm_sandbox_info);
#endif
  FALLIBLE_DYNAMIC_CHECK(
    infallible,
    get_info_func != nullptr,
    "mswasm could not find <MODULE_NAME>get_mswasm_sandbox_info");
  // return get_info_func();
  sandbox_info = get_info_func();

  // TODO: can't implement max memory without partitioned allocator.

  // 3) Invoke the sandbox's initialization routine
  sandbox = sandbox_info.create_mswasm_sandbox(sbox_argc, sbox_argv);
  FALLIBLE_DYNAMIC_CHECK(
    infallible, sandbox != nullptr, "Sandbox could not be created");

  // 4) store a handle to the sandbox's VmCtx
  exec_env = sandbox;

  return true;
}

#undef FALLIBLE_DYNAMIC_CHECK

inline void rlbox_mswasm_sandbox::impl_destroy_sandbox()
{
  // 1) free the return slot
  if (return_slot_size) {
    impl_free_in_sandbox(return_slot);
  }
  // 2) invoke the VmCtx destructor within the sandbox
  if (sandbox != nullptr) {
    sandbox_info.destroy_mswasm_sandbox(sandbox);
    sandbox = nullptr;
  }

// 3) dlclose the library
#ifndef RLBOX_USE_STATIC_CALLS
  if (library != nullptr) {
    dlclose(library);
    library = nullptr;
  }
#endif
}
} // namespace rlbox