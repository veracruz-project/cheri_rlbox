#pragma once

#include "rlbox_helpers.hpp"

// Use the same convention as rlbox to allow applications to customize the
// shared lock
#ifndef RLBOX_USE_CUSTOM_SHARED_LOCK
#  define RLBOX_SHARED_LOCK(name) std::shared_timed_mutex name
#  define RLBOX_ACQUIRE_SHARED_GUARD(name, ...)                                \
    std::shared_lock<std::shared_timed_mutex> name(__VA_ARGS__)
#  define RLBOX_ACQUIRE_UNIQUE_GUARD(name, ...)                                \
    std::unique_lock<std::shared_timed_mutex> name(__VA_ARGS__)
#else
#  if !defined(RLBOX_SHARED_LOCK) || !defined(RLBOX_ACQUIRE_SHARED_GUARD) ||   \
    !defined(RLBOX_ACQUIRE_UNIQUE_GUARD)
#    error                                                                     \
      "RLBOX_USE_CUSTOM_SHARED_LOCK defined but missing definitions for RLBOX_SHARED_LOCK, RLBOX_ACQUIRE_SHARED_GUARD, RLBOX_ACQUIRE_UNIQUE_GUARD"
#  endif
#endif

namespace rlbox {

#ifdef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES

rlbox_mswasm_sandbox_thread_data* get_rlbox_mswasm_sandbox_thread_data();
#  define RLBOX_mswasm_SANDBOX_STATIC_VARIABLES()                              \
    thread_local rlbox::rlbox_mswasm_sandbox_thread_data                       \
      rlbox_mswasm_sandbox_thread_info{ 0, 0 };                                \
    namespace rlbox {                                                          \
      rlbox_mswasm_sandbox_thread_data* get_rlbox_mswasm_sandbox_thread_data() \
      {                                                                        \
        return &rlbox_mswasm_sandbox_thread_info;                              \
      }                                                                        \
    }                                                                          \
    static_assert(true, "Enforce semi-colon")

#endif
}