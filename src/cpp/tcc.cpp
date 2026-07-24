#include "tcc.hpp"
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string>

void error_handler(void *_opaque, const char *msg) {
  // const char *service_name = static_cast<char *>(opaque);
  std::cerr << msg;
}

struct cjit {
  std::string name;
  TCCState *state = nullptr;
  cjit(const cjit &) = delete;
  cjit &operator=(const cjit &) = delete;
  cjit(cjit &&) = default;
  cjit &operator=(cjit &&) = default;

  explicit cjit(const std::string name) : name(name) {
    state = tcc_new();
    // tcc_set_lib_path(state, ".");
    tcc_set_options(state, "-nostdlib -Wall -Werror -bt 10");
    tcc_set_output_type(state, TCC_OUTPUT_MEMORY);
    tcc_set_error_func(state, (void *)this->name.c_str(), error_handler);
  }

  bool compile(const std::string &code, bool write) {
    if (write) {
      std::ofstream file(name);
      if (file.is_open()) {
        file << code;
        file.close();
      }
    }
    const auto result = tcc_compile_string(state, code.c_str());
    if (result != 0)
      return false;
    // tcc_relocate(state, TCC_RELOCATE_AUTO);
    return tcc_relocate(state) >= 0;
  }

  void *peek(const std::string &name) const {
    if (state) {
      void *result = tcc_get_symbol(state, name.c_str());
      if (not result)
        return nullptr;
      // throw std::runtime_error(fmt::format("no such symbol \"{}\" in service
      // \"{}\" ", name, this->name));
      return result;
    }
    return nullptr;
  }

  void push(const std::string &name, void *value) {
    if (state) {
      tcc_add_symbol(state, name.c_str(), value);
    }
  }

  ~cjit() {
    if (state)
      tcc_delete(state);
  }
};

uint64_t terranova::init() { return (uint64_t)new cjit("jit"); }

void terranova::deinit(uint64_t ctx) { delete (cjit *)ctx; }

bool terranova::compile(uint64_t ctx, const std::string &code) {
    auto c = ((cjit *)ctx);
    c->push("printf", (void*)printf);
  return c->compile(code, true);
}

uint64_t terranova::get_callable(uint64_t ctx, const std::string &name) {
    auto c = ((cjit *)ctx);
    return (uint64_t)c->peek(name);
}

void terranova::call(uint64_t func) {
  if (func) {
    ((void (*)())func)();
  }
}
