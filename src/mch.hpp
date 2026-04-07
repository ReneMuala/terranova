//
// Created by dte on 4/7/2026.
//

#pragma once
#include <vector>
#include <string>
#include <yyjson.h>

namespace mch
{
    enum type
    {
        raw,
        resolve,
        open_list,
        open_inverted,
        fetch,
        close,
        enable_escaping,
        disable_escaping,
        next,
    };

    struct node
    {
        enum type type;
        std::string str;
        size_t size = 1;

        bool operator==(const node& other) const
        {
            return type == other.type and str == other.str and size == other.size;
        }
    };

    struct helper
    {
        typedef std::function<std::string(const std::string& str, void*)> string_callback;
        typedef std::function<void(const std::string& str, void*)> void_callback;
        typedef std::function<bool(const std::string& str, void*)> bool_callback;
        const string_callback escaper;
        const string_callback resolver;
        const void_callback fetcher;
        const bool_callback list_opener;
        const bool_callback inversion_opener;
        const void_callback closer;
        const bool_callback nexter;
    };
    std::string render(std::vector<struct node> const& nodes, const helper& r_helper, void* buffer = nullptr);

    std::vector<node> parse(std::string const& str);

    namespace yyjson
    {
        // -------------------------------------------------------------------
        // Context structure passed as void* buffer to all callbacks
        // -------------------------------------------------------------------
        struct yyjson_render_context
        {
            yyjson_val* root; // document root

            struct frame
            {
                enum { OBJ, ARR, SCALAR } type;

                yyjson_val* val; // current value
                size_t idx; // for ARR: current element index
                size_t len; // for ARR: total elements
                bool fetch_instance; // breaks lookups for dotted values
            };

            std::vector<frame> stack; // context stack (top = back)

            explicit yyjson_render_context(yyjson_doc* doc)
                : root(yyjson_doc_get_root(doc))
            {
            }

            // Returns the current context value (top of stack or root)
            yyjson_val* current_value() const
            {
                if (stack.empty()) return root;
                const auto& top = stack.back();
                if (top.type == frame::OBJ || top.type == frame::SCALAR)
                    return top.val;
                // ARR: return current element
                return yyjson_arr_get(top.val, top.idx);
            }
        };

        helper make_yyjson_helper();
    }
}
