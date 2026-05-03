#include <algorithm>
#include <string>
#include <regex>
#include <string_view>
#include "routes.hpp"

namespace misc {

    inline std::string to_string(const std::u8string_view &str)
    {
        return {reinterpret_cast<const char *>(str.data()), str.length()};
    }

    inline std::string tolower(std::string str)
    {
        std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c)
                       { return std::tolower(c); });
        return str;
    }

    inline std::string toupper(std::string str)
    {
        std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c)
                       { return std::toupper(c); });
        return str;
    }
    inline const std::string &second_if_empty(const std::string &first, const std::string &second)
    {
        if (first.empty())
            return second;
        return first;
    }
        inline std::string throw_if_invalid_identifier(const std::string &identifier)
    {
        if (std::regex_match(identifier, std::regex(R"(\w+)")))
            return identifier;
        throw std::runtime_error(fmt::format("\"{}\" is not a valid identifier", identifier));
    }

    inline std::string to_route(const std::string &name, const bool with_prefix = true)
    {
        return (with_prefix ? routes::current_namespace : std::string("/")) + std::regex_replace(
                                                                                  tolower(name), std::regex("\\s"), "-");
    }

    inline std::string snake_to_kebab(const std::string &kebab)
    {
        return std::regex_replace(kebab, std::regex("_"), "-");
    }

    inline std::string remove_trailing_underline(const std::string_view &name)
    {
        return std::string(name.ends_with("_") ? name.substr(0, name.length() - 1) : name);
    }

}