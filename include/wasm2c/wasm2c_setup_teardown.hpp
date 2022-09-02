#pragma once

#include "wasm-rt.h"

// Pull the helper header from the main repo for dynamic_check and scope_exit
#include "rlbox_helpers.hpp"
#include "rlbox_wasm2c_sandbox.hpp"
#include "wasm2c_details.hpp"

namespace rlbox {
#define FALLIBLE_DYNAMIC_CHECK(infallible, cond, msg)                          \
  if (infallible) {                                                            \
    detail::dynamic_check(cond, msg);                                          \
  } else if (!(cond)) {                                                        \
    impl_destroy_sandbox();                                                    \
    return false;                                                              \
  }

/**
 * @brief creates the Wasm sandbox from the given shared library
 *
 * @param wasm2c_module_path path to shared library compiled with wasm2c. This
 * param is not specified if you are creating a statically linked sandbox.
 * @param infallible if set to true, the sandbox aborts on failure. If false,
 * the sandbox returns creation status as a return value
 * @param override_max_heap_size optional override of the maximum size of the
 * wasm heap allowed for this sandbox instance. When the value is zero, platform
 * defaults are used. Non-zero values are rounded to max(64k, next power of 2).
 * @param wasm_module_name optional module name used when compiling with wasm2c
 * @return true when sandbox is successfully created
 * @return false when infallible if set to false and sandbox was not
 * successfully created. If infallible is set to true, this function will never
 * return false.
 */
bool rlbox_wasm2c_sandbox::impl_create_sandbox(
#ifndef RLBOX_USE_STATIC_CALLS
  path_buf wasm2c_module_path,
#endif
  bool infallible = true,
  uint64_t override_max_heap_size = 0,
  const char* wasm_module_name = "")
{
  FALLIBLE_DYNAMIC_CHECK(
    infallible, sandbox == nullptr, "Sandbox already initialized");

#ifndef RLBOX_USE_STATIC_CALLS
#  if defined(_WIN32)
  library = (void*)LoadLibraryW(wasm2c_module_path);
#  else
  library = dlopen(wasm2c_module_path, RTLD_LAZY);
#  endif

  if (!library) {
    std::string error_msg = "Could not load wasm2c dynamic library: ";
#  if defined(_WIN32)
    DWORD errorMessageID = GetLastError();
    if (errorMessageID != 0) {
      LPSTR messageBuffer = nullptr;
      // The api creates the buffer that holds the message
      size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                     FORMAT_MESSAGE_FROM_SYSTEM |
                                     FORMAT_MESSAGE_IGNORE_INSERTS,
                                   NULL,
                                   errorMessageID,
                                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                   (LPSTR)&messageBuffer,
                                   0,
                                   NULL);
      // Copy the error message into a std::string.
      std::string message(messageBuffer, size);
      error_msg += message;
      LocalFree(messageBuffer);
    }
#  else
    error_msg += dlerror();
#  endif
    FALLIBLE_DYNAMIC_CHECK(infallible, false, error_msg.c_str());
  }
#endif

#ifndef RLBOX_USE_STATIC_CALLS
  std::string info_func_name = wasm_module_name;
  info_func_name += "get_wasm2c_sandbox_info";
  auto get_info_func = reinterpret_cast<wasm2c_sandbox_funcs_t (*)()>(
    symbol_lookup(info_func_name));
#else
  // only permitted if there is no custom module name
  std::string wasm_module_name_str = wasm_module_name;
  FALLIBLE_DYNAMIC_CHECK(
    infallible,
    wasm_module_name_str.empty(),
    "Static calls not supported with non empty module names");
  auto get_info_func =
    reinterpret_cast<wasm2c_sandbox_funcs_t (*)()>(get_wasm2c_sandbox_info);
#endif
  FALLIBLE_DYNAMIC_CHECK(
    infallible,
    get_info_func != nullptr,
    "wasm2c could not find <MODULE_NAME>get_wasm2c_sandbox_info");
  sandbox_info = get_info_func();

  std::call_once(wasm2c_runtime_initialized,
                 [&]() { sandbox_info.wasm_rt_sys_init(); });

  override_max_heap_size =
    rlbox_wasm2c_get_adjusted_heap_size(override_max_heap_size);
  const uint64_t override_max_wasm_pages =
    rlbox_wasm2c_get_heap_page_count(override_max_heap_size);
  FALLIBLE_DYNAMIC_CHECK(infallible,
                         override_max_wasm_pages <= 65536,
                         "Wasm allows a max heap size of 4GB");

  sandbox = sandbox_info.create_wasm2c_sandbox(
    static_cast<uint32_t>(override_max_wasm_pages));
  FALLIBLE_DYNAMIC_CHECK(
    infallible, sandbox != nullptr, "Sandbox could not be created");

  sandbox_memory_info =
    (wasm_rt_memory_t*)sandbox_info.lookup_wasm2c_nonfunc_export(sandbox,
                                                                 "w2c_memory");
  FALLIBLE_DYNAMIC_CHECK(infallible,
                         sandbox_memory_info != nullptr,
                         "Could not get wasm2c sandbox memory info");

  heap_base = reinterpret_cast<uintptr_t>(impl_get_memory_location());

  if constexpr (sizeof(uintptr_t) != sizeof(uint32_t)) {
    // On larger platforms, check that the heap is aligned to the pointer size
    // i.e. 32-bit pointer => aligned to 4GB. The implementations of
    // impl_get_unsandboxed_pointer_no_ctx and impl_get_sandboxed_pointer_no_ctx
    // below rely on this.
    uintptr_t heap_offset_mask = std::numeric_limits<T_PointerType>::max();
    FALLIBLE_DYNAMIC_CHECK(infallible,
                           (heap_base & heap_offset_mask) == 0,
                           "Sandbox heap not aligned to 4GB");
  }

  // cache these for performance
  exec_env = sandbox;
#ifndef RLBOX_USE_STATIC_CALLS
  malloc_index = impl_lookup_symbol("malloc");
  free_index = impl_lookup_symbol("free");
#else
  malloc_index = rlbox_wasm2c_sandbox_lookup_symbol(malloc);
  free_index = rlbox_wasm2c_sandbox_lookup_symbol(free);
#endif
  return true;
}

#undef FALLIBLE_DYNAMIC_CHECK

inline void rlbox_wasm2c_sandbox::impl_destroy_sandbox()
{
  if (return_slot_size) {
    impl_free_in_sandbox(return_slot);
  }

  if (sandbox != nullptr) {
    sandbox_info.destroy_wasm2c_sandbox(sandbox);
    sandbox = nullptr;
  }

#ifndef RLBOX_USE_STATIC_CALLS
  if (library != nullptr) {
#  if defined(_WIN32)
    FreeLibrary((HMODULE)library);
#  else
    dlclose(library);
#  endif
    library = nullptr;
  }
#endif
}
} // namespace rlbox