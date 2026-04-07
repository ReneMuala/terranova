#include <functional>
#include <iostream>
#include <stack>
#include <fmt/core.h>
#include "mch.hpp"
namespace mch
{
    std::string render(std::vector<struct node> const& nodes, const helper& r_helper, void* buffer)
    {
        std::string result;
        bool escaped = true;
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            switch (const auto& [type, str, size] = nodes[i]; type)
            {
            case type::raw:
                result += str;
                break;
            case type::resolve:
                result += escaped
                              ? r_helper.escaper(r_helper.resolver(str, buffer), buffer)
                              : r_helper.resolver(str, buffer);
                break;
            case type::open_list:
                if (not r_helper.list_opener(str, buffer))
                    i += size - 1;
                break;
            case type::open_inverted:
                if (not r_helper.inversion_opener(str, buffer))
                    i += size - 1;
                break;
            case type::fetch:
                r_helper.fetcher(str, buffer);
                break;
            case type::close:
                r_helper.closer("", buffer);
                break;
            case type::enable_escaping:
                escaped = true;
                break;
            case type::disable_escaping:
                escaped = false;
                break;
            case type::next:
                if (r_helper.nexter("", buffer))
                    i -= size - 1;
                break;
            default: ;
            }
        }
        return result;
    }

    inline bool lookup(const std::string& it, std::string const& str, const size_t offset)
    {
        const auto result = std::string_view(str.data() + offset, str.size() - offset).starts_with(it);
        return result;
    }

    inline bool expect(const std::string& it, std::string const& str, size_t& offset)
    {
        const auto result = std::string_view(str.data() + offset, str.size() - offset).starts_with(it);
        if (result)
        {
            offset += it.length();
        }
        return result;
    }

    std::string extract_id_or_dot(std::string const& str, size_t& offset, const std::string& open_pattern,
                                  const std::string& close_pattern)
    {
        size_t start = 0, limit = 0, trailing = 0;
        if (open_pattern != " ")
        {
            for (size_t i = offset; i < str.size() and iswspace(static_cast<unsigned char>(str[i])); ++i)
            {
                start++;
            }
        }
        const size_t id_begin_index = offset + start;
        if (id_begin_index < str.size() and str[id_begin_index] == '.')
        {
            limit++;
        }
        else
        {
            for (size_t i = id_begin_index; i < str.size() and (iswalpha(static_cast<unsigned char>(str[i])) or
                     iswdigit(static_cast<unsigned char>(str[i])) or str[i] == '_'); ++i)
            {
                limit++;
            }
        }

        if (close_pattern != " ")
        {
            for (size_t i = offset + start + limit; i < str.size() and iswspace(static_cast<unsigned char>(str[i])); ++
                 i)
            {
                trailing++;
            }
        }

        auto result = std::string(str.substr(offset + start, limit));
        offset += start + limit + trailing;
        return result;
    }

    std::string extract_non_space(std::string const& str, size_t& offset)
    {
        size_t limit = 0;
        for (size_t i = offset; i < str.size() and not iswspace(static_cast<unsigned char>(str[i])); ++i)
        {
            limit++;
        }
        auto result = std::string(str.substr(offset, limit));
        offset += limit;
        return result;
    }

    std::string extract_non_equal(std::string const& str, size_t& offset)
    {
        size_t limit = 0;
        for (size_t i = offset; i < str.size() and str[i] != '='; ++i)
        {
            limit++;
        }
        auto result = std::string(str.substr(offset, limit));
        offset += limit;
        return result;
    }

    bool extract_spaces(std::string const& str, size_t& offset)
    {
        bool found = false;
        while (offset < str.size() and iswspace(static_cast<unsigned char>(str[offset])))
        {
            if (not found) found = true;
            offset++;
        }
        return found;
    }

    inline void parse_comment(std::string const& str, size_t& offset, const std::string& close_pattern)
    {
        while (offset < str.size() and not lookup(close_pattern, str, offset))
            offset++;
    }

    void open_section(std::vector<node>& nodes, std::stack<std::tuple<mch::type, std::string, size_t>>& context_stack,
                      const std::string& id,
                      const mch::type t)
    {
        nodes.push_back(node(t, id));
        context_stack.push({t, id, nodes.size() - 1});
    }

    void close_section(std::vector<node>& nodes, std::stack<std::tuple<mch::type, std::string, size_t>>& context_stack,
                       const std::string& id,
                       size_t& offset)
    {
        if (context_stack.empty())
            throw std::runtime_error(
                fmt::format(R"(unexpected closing tag "{}" at offset {})", id, offset));
        auto& section = context_stack.top();
        if (std::get<1>(section) != id)
            throw std::runtime_error(fmt::format(
                R"(unexpected closing tag "{}" (it should be {}) at offset {})", id,
                std::get<1>(section), offset));
        if (std::get<0>(section) == mch::open_list)
        {
            nodes[std::get<2>(section)].size = nodes.size() - std::get<2>(section) + 2; // 2 - because of next and close
            nodes.push_back(node(next, id, nodes[std::get<2>(section)].size - 1));
            // tells the renderer to try to fetch for the next element if this is a list
        }
        else
        {
            nodes[std::get<2>(section)].size = nodes.size() - std::get<2>(section) + 1; // 1 - because of close
        }
        context_stack.pop();
        nodes.push_back(node(close, id));
    }

    inline void parse_delimiter(std::string const& str, std::string& new_open_pattern, std::string& new_close_pattern,
                                size_t& offset)
    {
        new_open_pattern = extract_non_space(str, offset);
        if (not extract_spaces(str, offset))
            throw std::runtime_error(fmt::format("expected space at offset {}", offset));
        new_close_pattern = extract_non_equal(str, offset);
        if (new_open_pattern.empty()) new_open_pattern = " "; // handle the case {{= =}}
        if (new_close_pattern.empty()) new_close_pattern = " "; // handle the case {{= =}}
        if (not expect("=", str, offset))
            throw std::runtime_error(fmt::format("expected = at offset {}", offset));
    }

    std::vector<node> parse(std::string const& str)
    {
        std::string open_pattern = "{{", close_pattern = "}}", new_open_pattern, new_close_pattern;
        size_t offset = 0;
        std::string buffer;
        std::vector<node> nodes;
        std::stack<std::tuple<mch::type, std::string, size_t>> context_stack;
        while (offset < str.size())
        {
            if (not new_open_pattern.empty())
            {
                open_pattern = new_open_pattern;
                new_open_pattern.clear();
            }
            if (not new_close_pattern.empty())
            {
                close_pattern = new_close_pattern;
                new_close_pattern.clear();
            }
            if (expect(open_pattern, str, offset))
            {
                if (not buffer.empty())
                {
                    nodes.push_back(node(mch::raw, buffer));
                    buffer.clear();
                }
                if (expect("=", str, offset))
                {
                    parse_delimiter(str, new_open_pattern, new_close_pattern, offset);
                }
                else if (expect("!", str, offset)) // comment
                    parse_comment(str, offset, close_pattern);
                else if (expect("#", str, offset)) // open section
                    open_section(nodes, context_stack, extract_id_or_dot(str, offset, open_pattern, close_pattern),
                                 mch::open_list);
                else if (expect("^", str, offset)) // open inverted section
                    open_section(nodes, context_stack, extract_id_or_dot(str, offset, open_pattern, close_pattern),
                                 mch::open_inverted);
                else if (expect("/", str, offset)) // close section
                    close_section(nodes, context_stack, extract_id_or_dot(str, offset, open_pattern, close_pattern),
                                  offset);
                else
                {
                    const bool unescaped = expect("{", str, offset);
                    if (unescaped)
                        nodes.push_back(node(mch::disable_escaping, ""));
                    std::string id = extract_id_or_dot(str, offset, open_pattern, close_pattern);
                    if (expect(".", str, offset))
                    {
                        size_t depth = 0;
                        bool has_dot = false;
                        nodes.push_back(node(mch::fetch, id));
                        do
                        {
                            depth++;
                            id = extract_id_or_dot(str, offset, open_pattern, close_pattern);
                            has_dot = expect(".", str, offset);
                            nodes.push_back(node(has_dot ? mch::fetch : mch::resolve, id));
                        }
                        while (has_dot);
                        for (size_t i = 0; i < depth; i++)
                            nodes.push_back(node(mch::close, ""));
                    }
                    else
                        nodes.push_back(node(resolve, id));
                    if (unescaped)
                        nodes.push_back(node(enable_escaping, ""));
                    if (unescaped and not expect("}", str, offset))
                        throw std::runtime_error(fmt::format(R"(expected to find "{}" at offset {})", "}", offset));
                }
                if (not expect(close_pattern, str, offset))
                    throw std::runtime_error(
                        fmt::format(R"(expected to find "{}" at offset {})", close_pattern, offset));
            }
            else
            {
                buffer += str[offset++];
            }
        }
        if (not buffer.empty())
        {
            nodes.push_back(node(raw, buffer));
            buffer.clear();
        }
        if (not context_stack.empty())
            throw std::runtime_error(
                fmt::format(R"(unclosed section {} at offset {})", std::get<1>(context_stack.top()), offset));
        return nodes;
    }
    namespace yyjson
{
    // -------------------------------------------------------------------
    // Utility: walk context stack TOP‑DOWN to find a key in an object
    // -------------------------------------------------------------------
    static yyjson_val* walk_stack_for_key(yyjson_render_context* ctx, const std::string& key)
    {
        // 1. Walk stack from top (back) to bottom (front)
        for (auto it = ctx->stack.rbegin(); it != ctx->stack.rend(); ++it)
        {
            yyjson_val* val = it->val;
            if (!val)
            {
                if (it->fetch_instance) return nullptr;
                continue;
            };
            if (it->type == yyjson_render_context::frame::ARR)
            {
                val = yyjson_arr_get(val, it->idx);
            }
            if (val && yyjson_is_obj(val))
            {
                yyjson_val* v = yyjson_obj_get(val, key.c_str());
                if (v) return v;
            }
            if (it->fetch_instance) return nullptr;
        }
        // 2. Finally check the root (bottom of implicit stack)
        if (ctx->root && yyjson_is_obj(ctx->root))
        {
            yyjson_val* v = yyjson_obj_get(ctx->root, key.c_str());
            if (v) return v;
        }
        return nullptr;
    }

    // -------------------------------------------------------------------
    // Convert a yyjson value to a string
    // -------------------------------------------------------------------
    static std::string val_to_string(yyjson_val* v)
    {
        if (!v) return "";
        switch (yyjson_get_type(v))
        {
        case YYJSON_TYPE_STR:
            return std::string(yyjson_get_str(v), yyjson_get_len(v));
        case YYJSON_TYPE_NUM:
            {
                // yyjson stores all numbers as double; we can check if it's integral
                double d = yyjson_get_num(v);
                if (yyjson_is_int(v))
                    // Integer: no decimal point
                    return fmt::format("{}", static_cast<int64_t>(yyjson_get_int(v)));
                // Float: default formatting removes trailing zeros
                return fmt::format("{}", d);
            }

        case YYJSON_TYPE_BOOL:
            return yyjson_get_bool(v) ? "true" : "false";

        case YYJSON_TYPE_NULL:
        default:
            return "";
        }
    }

    // -------------------------------------------------------------------
    // HTML escape (used by escaper callback)
    // -------------------------------------------------------------------
    static std::string html_escape(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (const char c : s)
        {
            switch (c)
            {
            case '&': out += "&amp;";
                break;
            case '<': out += "&lt;";
                break;
            case '>': out += "&gt;";
                break;
            case '"': out += "&quot;";
                break;
            case '\'': out += "&#39;";
                break;
            default: out += c;
            }
        }
        return out;
    }

    // -------------------------------------------------------------------
    // Push a new context frame based on a yyjson value
    // -------------------------------------------------------------------
    static void push_frame_for_value(yyjson_render_context* ctx, yyjson_val* v, bool fetch_instance = false)
    {
        if (!v)
        {
            ctx->stack.push_back({yyjson_render_context::frame::SCALAR, nullptr, 0, 0, fetch_instance});
            return;
        }
        switch (yyjson_get_type(v))
        {
        case YYJSON_TYPE_OBJ:
            ctx->stack.push_back({yyjson_render_context::frame::OBJ, v, 0, 0, fetch_instance});
            break;
        case YYJSON_TYPE_ARR:
            ctx->stack.push_back({yyjson_render_context::frame::ARR, v, 0, yyjson_arr_size(v), fetch_instance});
            break;
        default:
            ctx->stack.push_back({yyjson_render_context::frame::SCALAR, v, 0, 0, fetch_instance});
            break;
        }
    }

    // -------------------------------------------------------------------
    // Callback implementations
    // -------------------------------------------------------------------
    static std::string resolver_cb(const std::string& key, void* buf)
    {
        auto* ctx = static_cast<yyjson_render_context*>(buf);
        if (key == ".") return val_to_string(ctx->current_value());
        return val_to_string(walk_stack_for_key(ctx, key));
    }

    static std::string escaper_cb(const std::string& raw, void*)
    {
        return html_escape(raw);
    }

    static void fetcher_cb(const std::string& key, void* buf)
    {
        auto* ctx = static_cast<yyjson_render_context*>(buf);
        push_frame_for_value(ctx, walk_stack_for_key(ctx, key), true);
    }

    static bool list_opener_cb(const std::string& key, void* buf)
    {
        auto* ctx = static_cast<yyjson_render_context*>(buf);
        yyjson_val* v = walk_stack_for_key(ctx, key);

        // Falsy → skip section
        if (!v || yyjson_is_null(v)) return false;
        if (yyjson_is_bool(v) && !yyjson_get_bool(v)) return false;
        if (yyjson_is_arr(v) && yyjson_arr_size(v) == 0) return false;

        // Truthy → push context
        push_frame_for_value(ctx, v);
        return true;
    }

    static bool inversion_opener_cb(const std::string& key, void* buf)
    {
        auto* ctx = static_cast<yyjson_render_context*>(buf);
        yyjson_val* v = walk_stack_for_key(ctx, key);

        if (!v || yyjson_is_null(v)) return true;
        if (yyjson_is_bool(v) && !yyjson_get_bool(v)) return true;
        if (yyjson_is_arr(v) && yyjson_arr_size(v) == 0) return true;
        return false;
    }

    static void closer_cb(const std::string&, void* buf)
    {
        auto* ctx = static_cast<yyjson_render_context*>(buf);
        if (!ctx->stack.empty()) ctx->stack.pop_back();
    }

    static bool nexter_cb(const std::string&, void* buf)
    {
        auto* ctx = static_cast<yyjson_render_context*>(buf);
        if (ctx->stack.empty()) return false;
        auto& top = ctx->stack.back();
        if (top.type != yyjson_render_context::frame::ARR) return false;

        ++top.idx;
        if (top.idx < top.len) return true;
        ctx->stack.pop_back();
        return false;
    }

    // -------------------------------------------------------------------
    // Helper builder – returns a fully populated helper struct
    // -------------------------------------------------------------------
    mch::helper make_yyjson_helper()
    {
        return mch::helper{
            /* .escaper          = */ escaper_cb,
            /* .resolver         = */ resolver_cb,
            /* .fetcher          = */ fetcher_cb,
            /* .list_opener      = */ list_opener_cb,
            /* .inversion_opener = */ inversion_opener_cb,
            /* .closer           = */ closer_cb,
            /* .nexter           = */ nexter_cb
        };
    }
}

#ifdef  ENABLE_TESTS
    #define ASSERT_EQUAL(L,X,Y) \
    if(X == Y) std::cout << "[ " << __FUNCTION__ << "(" << L << ")" << ": " << #X << " == " << #Y << " PASSED ]\n"; \
    else std::cout << "[ " << __FUNCTION__ << ":" << __LINE__ << "(" << L << ")" << ": " << #X << " == " << #Y << " FAILED ]\n";

inline void test_parser()
{
    {
        std::vector<std::pair<std::string, std::vector<mch::node>>> cases = {
            std::make_pair(
                "first {{second}} third",
                std::vector<mch::node>{
                    mch::node(mch::raw, "first "),
                    mch::node(mch::resolve, "second"),
                    mch::node(mch::raw, " third")
                }),
            std::make_pair(
                "<h1>hello {{ name }}!</h1>",
                std::vector<mch::node>{
                    mch::node(mch::raw, "<h1>hello "),
                    mch::node(mch::resolve, "name"),
                    mch::node(mch::raw, "!</h1>")
                }),
            std::make_pair(
                R"(* {{html}}
    * {{{html}}})",
                std::vector<mch::node>{
                    mch::node(mch::raw, "* "),
                    mch::node(mch::resolve, "html"),
                    mch::node(mch::raw, R"(
    * )"),
                    mch::node(mch::disable_escaping, ""),
                    mch::node(mch::resolve, "html"),
                    mch::node(mch::enable_escaping, ""),
                }),
            std::make_pair(
                R"(Names:
    {{#tasks}}{{title}} - {{description}}{{/tasks}})",
                std::vector<mch::node>{
                    mch::node(mch::raw, R"(Names:
    )"),
                    mch::node(mch::open_list, R"(tasks)", 6),
                    mch::node(mch::resolve, R"(title)"),
                    mch::node(mch::raw, R"( - )"),
                    mch::node(mch::resolve, R"(description)"),
                    mch::node(mch::next, R"(tasks)", 5),
                    mch::node(mch::close, R"(tasks)"),
                }),
            std::make_pair(
                R"({{^logged}}<button>login</button>{{/logged}})",
                std::vector<mch::node>{
                    mch::node(mch::open_inverted, R"(logged)", 3),
                    mch::node(mch::raw, R"(<button>login</button>)"),
                    mch::node(mch::close, R"(logged)"),
                }),
            std::make_pair(
                R"({{greeting}}, {{name}}. You have {{count}} messages.)",
                std::vector<mch::node>{
                    mch::node(mch::resolve, R"(greeting)"),
                    mch::node(mch::raw, R"(, )"),
                    mch::node(mch::resolve, R"(name)"),
                    mch::node(mch::raw, R"(. You have )"),
                    mch::node(mch::resolve, R"(count)"),
                    mch::node(mch::raw, R"( messages.)"),
                }),
            std::make_pair(
                R"({{#items}}{{name}}{{/items}})",
                std::vector<mch::node>{
                    mch::node(mch::open_list, R"(items)", 4),
                    mch::node(mch::resolve, R"(name)"),
                    mch::node(mch::next, R"(items)", 3),
                    mch::node(mch::close, R"(items)"),
                }),
            std::make_pair(
                R"({{^empty}}No results found.{{/empty}})",
                std::vector<mch::node>{
                    mch::node(mch::open_inverted, R"(empty)", 3),
                    mch::node(mch::raw, R"(No results found.)"),
                    mch::node(mch::close, R"(empty)"),
                }),
            std::make_pair(
                R"({{{raw_html}}})",
                std::vector<mch::node>{
                    mch::node(mch::disable_escaping, R"()"),
                    mch::node(mch::resolve, R"(raw_html)"),
                    mch::node(mch::enable_escaping, R"()"),
                }),
            std::make_pair(
                R"({{user.name}})",
                std::vector<mch::node>{
                    mch::node(mch::fetch, R"(user)"),
                    mch::node(mch::resolve, R"(name)"),
                }),
            std::make_pair(
                R"({{user.address.city}})",
                std::vector<mch::node>{
                    mch::node(mch::fetch, R"(user)"),
                    mch::node(mch::fetch, R"(address)"),
                    mch::node(mch::resolve, R"(city)"),
                }),
            std::make_pair(
                R"({{.}})",
                std::vector<mch::node>{
                    mch::node(mch::resolve, R"(.)"),
                }),
            std::make_pair(
                R"({{#items}}{{.}}{{/items}})",
                std::vector<mch::node>{
                    mch::node(mch::open_list, R"(items)", 4),
                    mch::node(mch::resolve, R"(.)"),
                    mch::node(mch::next, R"(items)", 3),
                    mch::node(mch::close, R"(items)"),
                }),
            std::make_pair(
                R"({{  name  }})",
                std::vector<mch::node>{
                    mch::node(mch::resolve, R"(name)"),
                }),
            std::make_pair(
                R"({{name}}
)",
                std::vector<mch::node>{
                    mch::node(mch::resolve, R"(name)"),
                    mch::node(mch::raw, R"(
)"),
                }),
            std::make_pair(
                R"(Hello{{! this is a comment }}, {{name}}!)",
                std::vector<mch::node>{
                    mch::node(mch::raw, R"(Hello)"),
                    mch::node(mch::raw, R"(, )"),
                    mch::node(mch::resolve, R"(name)"),
                    mch::node(mch::raw, R"(!)"),
                }),
            std::make_pair(
                R"({{! comment without closing space}}{{name}})",
                std::vector<mch::node>{
                    mch::node(mch::resolve, R"(name)"),
                }),
            std::make_pair(
                R"({{!}}{{name}})",
                std::vector<mch::node>{
                    mch::node(mch::resolve, R"(name)"),
                }),
            std::make_pair(
                R"({{=<% %>=}}<%name%>)",
                std::vector<mch::node>{
                    mch::node(mch::resolve, R"(name)"),
                }),
            std::make_pair(
                R"({{=| |=}} |name| )",
                std::vector<mch::node>{
                    mch::node(mch::raw, R"( )"),
                    mch::node(mch::resolve, R"(name)"),
                    mch::node(mch::raw, R"( )"),
                }),
            std::make_pair(
                R"({{=<< >>=}}<<name>><<={{ }}=>>{{name}})",
                std::vector<mch::node>{
                    mch::node(mch::resolve, R"(name)"),
                    mch::node(mch::resolve, R"(name)"),
                }),
            std::make_pair(
                R"({{user_id}})",
                std::vector<mch::node>{
                    mch::node(mch::resolve, R"(user_id)"),
                }),
            std::make_pair(
                R"({{item2}})",
                std::vector<mch::node>{
                    mch::node(mch::resolve, R"(item2)"),
                }),
            std::make_pair(
                R"({{first_name}} {{last_name}})",
                std::vector<mch::node>{
                    mch::node(mch::resolve, R"(first_name)"),
                    mch::node(mch::raw, R"( )"),
                    mch::node(mch::resolve, R"(last_name)"),
                }),
            std::make_pair(
                R"({{#section_1}}{{value_2}}{{/section_1}})",
                std::vector<mch::node>{
                    mch::node(mch::open_list, R"(section_1)", 4),
                    mch::node(mch::resolve, R"(value_2)"),
                    mch::node(mch::next, R"(section_1)", 3),
                    mch::node(mch::close, R"(section_1)"),
                }),
            std::make_pair(
                R"({{#a}}{{#b}}{{#c}}{{value}}{{/c}}{{/b}}{{/a}})",
                std::vector<mch::node>{
                    mch::node(mch::open_list, R"(a)", 10),
                    mch::node(mch::open_list, R"(b)", 7),
                    mch::node(mch::open_list, R"(c)", 4),
                    mch::node(mch::resolve, R"(value)"),
                    mch::node(mch::next, R"(c)", 3),
                    mch::node(mch::close, R"(c)"),
                    mch::node(mch::next, R"(b)", 6),
                    mch::node(mch::close, R"(b)"),
                    mch::node(mch::next, R"(a)", 9),
                    mch::node(mch::close, R"(a)"),
                }),
            std::make_pair(
                R"({{#outer}}{{#inner}}{{.}}{{/inner}}{{/outer}})",
                std::vector<mch::node>{
                    mch::node(mch::open_list, R"(outer)", 7),
                    mch::node(mch::open_list, R"(inner)", 4),
                    mch::node(mch::resolve, R"(.)"),
                    mch::node(mch::next, R"(inner)", 3),
                    mch::node(mch::close, R"(inner)"),
                    mch::node(mch::next, R"(outer)", 6),
                    mch::node(mch::close, R"(outer)"),
                }),
            std::make_pair(
                R"({{^list}}empty{{/list}})",
                std::vector<mch::node>{
                    mch::node(mch::open_inverted, R"(list)", 3),
                    mch::node(mch::raw, R"(empty)"),
                    mch::node(mch::close, R"(list)"),
                }),
            std::make_pair(
                R"({{#list}}{{name}}{{/list}}{{^list}}(none){{/list}})",
                std::vector<mch::node>{
                    mch::node(mch::open_list, R"(list)", 4),
                    mch::node(mch::resolve, R"(name)"),
                    mch::node(mch::next, R"(list)", 3),
                    mch::node(mch::close, R"(list)"),
                    mch::node(mch::open_inverted, R"(list)", 3),
                    mch::node(mch::raw, R"((none))"),
                    mch::node(mch::close, R"(list)"),
                }),
            std::make_pair(
                R"({{= =}} a  b  ={{ }}= {{c}})",
                std::vector<mch::node>{
                    mch::node(mch::resolve, R"(a)"),
                    mch::node(mch::resolve, R"(b)"),
                    mch::node(mch::resolve, R"(c)"),
                }),
        };
        for (int i = 0; i < cases.size(); i++)
        {
            auto& [str, nodes] = cases[i];
            std::vector<mch::node> parser_output = mch::parse(str);
            std::vector<mch::node> expected_output = nodes;
            ASSERT_EQUAL(fmt::format("interpolation#{}", i), parser_output, expected_output);
        }
    }
}
#endif

}