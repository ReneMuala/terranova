#pragma once
#include "rust/cxx.h"
#include <libtcc.h>
#include <optional>
#include <stdint.h>
#include <string>
namespace terranova {
// struct FunctionWrapper {
//     void (*callback)();
// };
uint64_t init();
void deinit(uint64_t);
bool compile(uint64_t ctx, const std::string &code);
uint64_t get_callable(uint64_t ctx, const std::string &name);
void call(uint64_t func);
} // namespace terranova
