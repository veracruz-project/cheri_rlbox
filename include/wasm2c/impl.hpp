#pragma once

#include "wasm-rt.h"

#include "rlbox_helpers.hpp"
#include "rlbox_synchronize.hpp"
#include "rlbox_wasm2c_sandbox.hpp"
#include "wasm2c_callback.hpp"
#include "wasm2c_details.hpp"
#include "wasm2c_invoke_func_ptr.hpp"
#include "wasm2c_misc.hpp"
#include "wasm2c_setup_teardown.hpp"
#include "wasm2c_swizzle.hpp"

using rlbox::rlbox_wasm2c_sandbox;

namespace rlbox {

// declare the static symbol with weak linkage to keep this header only
#if defined(_MSC_VER)
__declspec(selectany)
#else
__attribute__((weak))
#endif
  std::once_flag rlbox_wasm2c_sandbox::wasm2c_runtime_initialized;

} // namespace rlbox
