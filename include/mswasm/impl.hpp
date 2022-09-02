#pragma once

#include "mswasm_callback.hpp"
#include "mswasm_details.hpp"
#include "mswasm_invoke_func_ptr.hpp"
#include "mswasm_misc.hpp"
#include "mswasm_setup_teardown.hpp"
#include "mswasm_swizzle.hpp"
#include "rlbox_helpers.hpp"
#include "rlbox_mswasm_sandbox.hpp"
#include "rlbox_synchronize.hpp"

using rlbox::rlbox_mswasm_sandbox;

namespace rlbox {

// declare the static symbol with weak linkage to keep this header only
#if defined(_MSC_VER)
__declspec(selectany)
#else
__attribute__((weak))
#endif
  std::once_flag rlbox_mswasm_sandbox::mswasm_runtime_initialized;

} // namespace rlbox
