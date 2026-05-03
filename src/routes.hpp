#pragma once

#include <regex>
#include <string>
#include <fmt/core.h>

namespace routes
{
    static std::string current_namespace;

    class namespace_lock
    {
    public:
        namespace_lock(const std::string& ns)
        {
            static std::regex valid_namespace(R"((/\w+)*/)");
            if (not std::regex_match(ns, valid_namespace))
            {
                throw std::runtime_error(fmt::format(
                    "invalid namespace \"{}\", namespaces must have the format /a/b/c.../d/ or just /", ns));
            }
            current_namespace = ns;
        }

        ~namespace_lock()
        {
            current_namespace = "/";
        }
    };
}