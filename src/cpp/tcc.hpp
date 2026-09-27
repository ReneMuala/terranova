#pragma once
#include "rust/cxx.h"
#include <libtcc.h>
#include <memory>
#include <optional>
#include <stdint.h>
#include <string>
namespace terranova {
uint64_t init();
void deinit(uint64_t);
bool compile(uint64_t ctx, const std::string &code);
uint64_t get_callable(uint64_t ctx, const std::string &name);
void call(uint64_t func);
std::unique_ptr<std::string> call_ret_str(uint64_t func);
std::unique_ptr<std::string> call_ret_str_str(uint64_t func, const std::string & route);
std::unique_ptr<std::string> call_ret_str_str_str(uint64_t func, const std::string & route, const std::string & body);
} // namespace terranova
