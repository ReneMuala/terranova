#pragma once
#include <fstream>
#include <string>
#include "types.hpp"
#include <list>
#include <vector>
#include <utility>
#include <libtcc.h>
#include <fmt/core.h>

namespace service
{
    void error_handler(void* opaque, const char* msg);
    void free_all_tracked_malloc();
    struct cjit
    {
        std::string name;
        TCCState *state = nullptr;
        cjit(const cjit &) = delete;
        cjit &operator=(const cjit &) = delete;
        cjit(cjit &&) = default;
        cjit &operator=(cjit &&) = default;

        explicit cjit(const std::string name) : name(name)
        {
            state = tcc_new();
            // tcc_set_lib_path(state, ".");
            tcc_set_options(state, "-nostdlib -Wall -Werror -bt 10");
            tcc_set_output_type(state, TCC_OUTPUT_MEMORY);
            tcc_set_error_func(state, (void *)this->name.c_str(), error_handler);
        }

        void compile(const std::string &code, bool write)
        {
            if(write){
                std::ofstream file(name);
                if (file.is_open()) {
                    file << code;
                    file.close();
                }
            }
            const auto result = tcc_compile_string(state, code.c_str());
            if (result != 0)
                throw std::runtime_error(fmt::format("load failed in service \"{}\"", this->name));
            tcc_relocate(state, TCC_RELOCATE_AUTO);            
        }

        void *peek(const std::string &name) const
        {
            if (state)
            {
                void *result = tcc_get_symbol(state, name.c_str());
                if (not result)
                    throw std::runtime_error(fmt::format("no such symbol \"{}\" in service \"{}\" ", name, this->name));
                return result;
            }
            return nullptr;
        }

        void push(const std::string &name, void *value)
        {
            if (state)
            {
                tcc_add_symbol(state, name.c_str(), value);
            }
        }

        ~cjit()
        {
            if (state)
                tcc_delete(state);
        }
    };

    std::pair<std::vector<generated_implementation>, std::string> init_services(
        std::vector<prepared_statement_metadata> &queries);
}