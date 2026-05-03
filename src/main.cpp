#include <any>
#include <exception>
#include <fstream>
#include <iostream>
#include <regex>
#include <fmt/core.h>
#include "kdlpp.h"
#include <glog/logging.h>
#include "iguana/ylt/reflection/member_value.hpp"
#include <stdexcept>
#include <yyjson.h>
#include <drogon/drogon_callbacks.h>
#include "uriparser/Uri.h"
#include "types.hpp"
#include <sqlite3.h>
#include <unordered_set>
#include "CLI11.hpp"
#include "mch.hpp"
#include <list>
#include "authentication.hpp"
#include "misc.hpp"
#include "db.hpp"
#include "service.hpp"
// #include <rpc/client.h>
std::unordered_map<std::string, std::any> handlers;

template <typename T>
bool has_field(const std::string& name)
{
    try
    {
        T type;
        ylt::reflection::get(type, name);
        return true;
    }
    catch (std::exception& e)
    {
        return false;
    }
}

template <typename T>
inline std::string get_canonical_name()
{
    using namespace ylt::reflection;
    std::string cann_name = misc::snake_to_kebab(fmt::format("{}(", ylt::reflection::get_struct_name<T>()));
    T type;
    bool fetched_fields = false;
    for_each(type, [&cann_name, &fetched_fields](auto& field, auto name, auto index)
    {
        if (name.starts_with("_")) return;
        std::string type_name;
        if constexpr (std::is_same_v<decltype(field), std::string&>)
            type_name = "string";
        else if constexpr (std::is_same_v<decltype(field), int&>)
            type_name = "int";
        else if constexpr (std::is_same_v<decltype(field), double&>)
            type_name = "double";
        else if constexpr (std::is_same_v<decltype(field), bool&>)
            type_name = "bool";
        auto fixed_name = misc::snake_to_kebab(misc::remove_trailing_underline(name));
        if (type_name.empty())
        {
            cann_name += fmt::format("{}, ", fixed_name);
        }
        else
        {
            cann_name += fmt::format("({}){}, ", type_name, fixed_name);
        }
        fetched_fields = true;
    });
    if (fetched_fields)
    {
        cann_name.pop_back();
        cann_name.pop_back();
    }
    cann_name += ")";
    return cann_name;
}

inline std::string to_string(const kdl::Type& type)
{
    switch (type)
    {
    case kdl::Type::String:
        return "string";
    case kdl::Type::Bool:
        return "bool";
    case kdl::Type::Number:
        return "int or double";
    default:
        return "unknown";
    }
}

template <typename T>
void load(kdl::Node& node, T& type);

template <typename T>
void get_loader()
{
    const auto struct_name = ylt::reflection::get_struct_name<T>();
    std::function<void()> loader = load<T>;
    if (handlers.contains(struct_name))
        handlers[struct_name] = loader;
    return handlers[struct_name];
}

static unsigned long long _global_index = 1;

template <typename T>
void load(kdl::Node& node, T& type)
{
    using namespace ylt::reflection;
    const auto struct_name = get_struct_name<T>();
    if (has_field<T>("_index"))
    {
        auto& _index = ylt::reflection::get<unsigned long long>(type, "_index");
        _index = _global_index++;
    }
    if (has_field<T>("_4x_padded_index"))
    {
        auto& _4x_padded_index = ylt::reflection::get<unsigned long long>(type, "_4x_padded_index");
        _4x_padded_index = _global_index;
        _global_index+=4ull;
    }
    if (has_field<T>("_comments"))
    {
        auto& _comments = ylt::reflection::get<std::string>(type, "_comments");
        _comments = node.comments();
    }
    if (has_field<T>("name"))
    {
        auto& name = ylt::reflection::get<std::string>(type, "name");
        if (node.args().empty()) throw std::runtime_error(fmt::format("\"{}\" should have a name", struct_name));
        if (node.args().size() > 1)
            throw std::runtime_error(
                fmt::format("\"{0}\" should only have the name argument (eg: {0} \"name-here\"...)", struct_name));
        const kdl::Value& value = node.args()[0];
        if (value.type() != kdl::Type::String)
            throw std::runtime_error(
                fmt::format("\"{}\"'s name must be a string", struct_name));
        name = misc::to_string(value.as<std::u8string>());
        if (name.empty()) throw std::runtime_error(fmt::format("\"{}\"'s name cannot be empty", struct_name));
    }
    for (const auto& prop : node.properties())
    {
        bool success = false;
        const auto prop_name = std::string(reinterpret_cast<const char*>(prop.first.data()), prop.first.length());
        for_each(type, [&](auto& field, auto name, auto index)
        {
            if (prop_name == misc::snake_to_kebab(misc::remove_trailing_underline(name)))
            {
                success = true;
                try
                {
                    if constexpr (std::is_same_v<decltype(field), std::string&>)
                        field = misc::to_string(prop.second.as<std::u8string>());
                    else if constexpr (std::is_same_v<decltype(field), int&>)
                        field = prop.second.as<int>();
                    else if constexpr (std::is_same_v<decltype(field), double&>)
                        field = prop.second.as<double>();
                    else if constexpr (std::is_same_v<decltype(field), bool&>)
                        field = prop.second.as<bool>();
                    else
                        success = false;
                }
                catch (kdl::TypeError& e)
                {
                    std::string field_type = {
                        get_struct_name<decltype(field)>().data(), get_struct_name<decltype(field)>().length() - 1
                    };
                    throw std::runtime_error(fmt::format("property \"{}\" should be \"{}\" (not \"{}\")",
                                                         misc::snake_to_kebab(prop_name),
                                                         field_type,
                                                         to_string(prop.second.type())));
                }
            }
        });
        if (not success)
            throw std::runtime_error(fmt::format("property \"{}\" is not supported in \"{}\"",
                                                 misc::snake_to_kebab(prop_name),
                                                 get_canonical_name<T>()));
    }
    for (auto& child : node.children())
    {
        bool success = false;
        const auto child_name = misc::to_string(child.name());
        for_each(type, [&](auto& it, auto name, auto index)
        {
            if constexpr (std::is_same_v<decltype(it), std::vector<entity>&>)
            {
                if (child_name == "entity")
                {
                    load<entity>(child, it.emplace_back());
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), schema&>)
            {
                if (child_name == "schema")
                {
                    load<schema>(child, it);
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), hooks&>)
            {
                if (child_name == "hooks")
                {
                    load<hooks>(child, it);
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), queries&>)
            {
                if (child_name == "queries")
                {
                    load<queries>(child, it);
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), auth&>)
            {
                if (child_name == "auth")
                {
                    load<auth>(child, it);
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), std::vector<before>&>)
            {
                if (child_name == "before")
                {
                    load<before>(child, it.emplace_back());
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), std::vector<after>&>)
            {
                if (child_name == "after")
                {
                    load<after>(child, it.emplace_back());
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), std::vector<field>&>)
            {
                if (child_name == "field")
                {
                    load<field>(child, it.emplace_back());
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), std::vector<has_one>&>)
            {
                if (child_name == "has-one")
                {
                    load<has_one>(child, it.emplace_back());
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), std::vector<has_many>&>)
            {
                if (child_name == "has-many")
                {
                    load<has_many>(child, it.emplace_back());
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), std::vector<belongs_to>&>)
            {
                if (child_name == "belongs-to")
                {
                    load<belongs_to>(child, it.emplace_back());
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), std::optional<pk>&>)
            {
                if (child_name == "pk")
                {
                    ::pk temp;
                    load<::pk>(child, temp);
                    it = temp;
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), std::optional<file_options>&>)
            {
                if (child_name == "file-options")
                {
                    ::file_options temp;
                    load<::file_options>(child, temp);
                    it = temp;
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), std::vector<struct get>&>)
            {
                if (child_name == "get")
                {
                    load<struct get>(child, it.emplace_back());
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), std::vector<post>&>)
            {
                if (child_name == "post")
                {
                    load<post>(child, it.emplace_back());
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), std::vector<put>&>)
            {
                if (child_name == "put")
                {
                    load<put>(child, it.emplace_back());
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), std::vector<delete_>&>)
            {
                if (child_name == "delete")
                {
                    load<delete_>(child, it.emplace_back());
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), std::vector<param>&>)
            {
                if (child_name == "param")
                {
                    load<param>(child, it.emplace_back());
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), std::vector<profile>&>)
            {
                if (child_name == "profile")
                {
                    load<profile>(child, it.emplace_back());
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), std::vector<listen>&>)
            {
                if (child_name == "listen")
                {
                    load<listen>(child, it.emplace_back());
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), views&>)
            {
                if (child_name == "views")
                {
                    load<views>(child, it);
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), std::vector<template_>&>)
            {
                if (child_name == "template")
                {
                    load<template_>(child, it.emplace_back());
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), std::vector<for_>&>)
            {
                if (child_name == "for")
                {
                    load<for_>(child, it.emplace_back());
                    success = true;
                }
            }
            else if constexpr (std::is_same_v<decltype(it), std::vector<template_set>&>)
            {
                if (child_name == "template-set")
                {
                    load<template_set>(child, it.emplace_back());
                    success = true;
                }
            } else if constexpr (std::is_same_v<decltype(it), std::vector<data>&>)
            {
                if (child_name == "data")
                {
                    load<data>(child, it.emplace_back());
                    success = true;
                }
            } else if constexpr (std::is_same_v<decltype(it), std::vector<bind>&>)
            {
                if (child_name == "bind")
                {
                    load<bind>(child, it.emplace_back());
                    success = true;
                }
            } else if constexpr (std::is_same_v<decltype(it), std::vector<role>&>)
            {
                if (child_name == "role")
                {
                    load<role>(child, it.emplace_back());
                    success = true;
                }
            } /*else if constexpr (std::is_same_v<decltype(it), std::vector<struct rpc_>&>)
            {
                if (child_name == "rpc")
                {
                    load<struct rpc_>(child, it.emplace_back());
                    success = true;
                }
            }*/
        });
        if (not success)
            throw std::runtime_error(
                fmt::format("child \"{}\" is not supported in \"{}\"", child_name, get_canonical_name<T>()));
    }
}

void load(kdl::Document& document, std::vector<application>& apps)
{
    for (auto& node : document.nodes())
    {
        if (node.name() == u8"application")
        {
            load<application>(node, apps.emplace_back());
        } else {
            throw std::runtime_error(fmt::format("root node type \"{}\" is not supported", (char*)node.name().c_str()));
        }
    }
}


void native()
{
    std::cout << "native function called" << std::endl;
}

bool prepared_statement_reset(void* prepared_statement)
{
    SQLite::Statement* stat = (SQLite::Statement*)prepared_statement;
    if (stat)
    {
        try
        {
            stat->reset();
            stat->clearBindings();
            return true;
        }
        catch (SQLite::Exception& ex)
        {
            return false;
        }
    }
    return false;
}

void bind_statement_const_char(void* prepared_statement, const char* name, const char* value)
{
    SQLite::Statement* stat = (SQLite::Statement*)prepared_statement;
    if (stat and name and value)
    {
        stat->bind(name, value);
    }
}

void bind_statement_int(void* prepared_statement, const char* name, int value)
{
    SQLite::Statement* stat = (SQLite::Statement*)prepared_statement;
    if (stat)
    {
        stat->bind(name, value);
    }
}

void bind_statement_float(void* prepared_statement, const char* name, float value)
{
    SQLite::Statement* stat = (SQLite::Statement*)prepared_statement;
    if (stat)
    {
        stat->bind(name, value);
    }
}

void collect_int(const char* name, void* output_buffer, void* input_buffer)
{
    if (name && output_buffer && input_buffer)
        *static_cast<int*>(output_buffer) =
            yyjson_get_int(yyjson_obj_get(static_cast<yyjson_val*>(input_buffer), name));
}

void collect_float(const char* name, void* output_buffer, void* input_buffer)
{
    if (name && output_buffer && input_buffer)
        *static_cast<float*>(output_buffer) = yyjson_get_real(
            yyjson_obj_get(static_cast<yyjson_val*>(input_buffer), name));
}

void collect_bool(const char* name, void* output_buffer, void* input_buffer)
{
    if (name && output_buffer && input_buffer)
        *static_cast<bool*>(output_buffer) = yyjson_get_bool(
            yyjson_obj_get(static_cast<yyjson_val*>(input_buffer), name));
}

char * fake_strdup(const char * src)
{
    if (not src) return nullptr;
    return strdup(src);
}

void collect_const_char(const char* name, void** output_buffer, void* input_buffer)
{
    if (name && output_buffer && input_buffer)
        *(char**)output_buffer = fake_strdup(yyjson_get_str(yyjson_obj_get(static_cast<yyjson_val*>(input_buffer), name)));
}

typedef bool (*output_handler_t)(void* obj, void* ou, void* in);

int to_sqlite_type(const std::string& name)
{
    if (name == "INTEGER")
        return SQLite::INTEGER;
    if (name == "FLOAT")
        return SQLite::FLOAT;
    if (name == "TEXT")
        return SQLite::TEXT;
    if (name == "BLOB")
        return SQLite::BLOB;
    return SQLite::TEXT;
}
void error_handler(const char* what, const char* message, void* context);

const char * prepared_statement_finish_results_json(void * result)
{
    if (result)
    {
        yyjson_mut_doc* output_doc = (yyjson_mut_doc*)result;
        char* body = yyjson_mut_write(output_doc, YYJSON_WRITE_ESCAPE_UNICODE, NULL);
        yyjson_mut_doc_free(output_doc);
        return body;
    }
    return NULL;
}

void prepared_statement_append_results_json(void ** result, void ** root_field,const char * section,void* prepared_statement, void * context)
{
    SQLite::Statement* stat = (SQLite::Statement*)prepared_statement;
    yyjson_mut_val* output_root = (yyjson_mut_val*)*root_field;
    yyjson_mut_doc* output_doc = (yyjson_mut_doc*)*result;
    if (not *result)
    {
        *result = yyjson_mut_doc_new(NULL);
        output_doc = (yyjson_mut_doc*)*result;
        *root_field = yyjson_mut_obj(output_doc);
        output_root = (yyjson_mut_val*)*root_field;
        yyjson_mut_doc_set_root(output_doc, output_root);
    }
    yyjson_mut_val* data_arr = yyjson_mut_arr(output_doc);
    unsigned long long count = 0;
    int col_count = -1;
    try
    {
        while (stat->executeStep())
        {
            if (col_count == -1)
                col_count = stat->getColumnCount();
            count++;
            yyjson_mut_val* data_item = yyjson_mut_obj(output_doc);
            for (int i = 0; i < col_count; ++i)
            {
                const auto& column = stat->getColumn(i);
                const auto type = column.getType();
                if (type == SQLite::INTEGER)
                    yyjson_mut_obj_add_int(output_doc, data_item, column.getName(), stat->getColumn(i).getInt());
                else if (type == SQLite::FLOAT)
                    yyjson_mut_obj_add_double(output_doc, data_item, column.getName(), stat->getColumn(i).getDouble());
                else if (type == SQLite::TEXT or type == SQLite::BLOB)
                    yyjson_mut_obj_add_strcpy(output_doc, data_item, column.getName(), stat->getColumn(i).getText());
                else if (type == SQLite::Null)
                    yyjson_mut_obj_add_null(output_doc, data_item, column.getName());
            }
            yyjson_mut_arr_add_val(data_arr, data_item);
        }
    }  catch (std::exception & e)
    {
        error_handler(__FUNCTION__, e.what(), context);
        return;
    }
    const auto current_obj = yyjson_mut_obj(output_doc);
    yyjson_mut_obj_add_val(output_doc, output_root, section, current_obj);
    yyjson_mut_obj_add_val(output_doc, current_obj, "data", data_arr);
    yyjson_mut_obj_add_uint(output_doc, current_obj, "count", count);
    if (stat->getErrorCode() == 0)
        yyjson_mut_obj_add_str(output_doc, current_obj, "error", stat->getErrorMsg());
    else
        yyjson_mut_obj_add_null(output_doc, current_obj, "error");
    yyjson_mut_obj_add_int(output_doc, current_obj, "modified", stat->getChanges());
}

const char* prepared_statement_get_results_json(void* prepared_statement, void * context)
{
    SQLite::Statement* stat = (SQLite::Statement*)prepared_statement;
    yyjson_mut_doc* output_doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val* output_root = yyjson_mut_obj(output_doc);
    yyjson_mut_doc_set_root(output_doc, output_root);
    yyjson_mut_val* data_arr = yyjson_mut_arr(output_doc);
    unsigned long long count = 0;
    int col_count = -1;
    try
    {
    while (stat->executeStep())
    {
        if (col_count == -1)
            col_count = stat->getColumnCount();
        count++;
        yyjson_mut_val* data_item = yyjson_mut_obj(output_doc);
        for (int i = 0; i < col_count; ++i)
        {
            // LOG(ERROR) << __FILE__ << "/" << __FUNCTION__ << ":" << __LINE__;
            const auto& column = stat->getColumn(i);
            const auto type = column.getType();
            if (type == SQLite::INTEGER)
                yyjson_mut_obj_add_int(output_doc, data_item, column.getName(), stat->getColumn(i).getInt());
            else if (type == SQLite::FLOAT)
                yyjson_mut_obj_add_double(output_doc, data_item, column.getName(), stat->getColumn(i).getDouble());
            else if (type == SQLite::TEXT or type == SQLite::BLOB)
                yyjson_mut_obj_add_strcpy(output_doc, data_item, column.getName(), stat->getColumn(i).getText());
            else if (type == SQLite::Null)
                yyjson_mut_obj_add_null(output_doc, data_item, column.getName());
        }
        yyjson_mut_arr_add_val(data_arr, data_item);
    }
} catch (std::exception & e)
{
    error_handler(__FUNCTION__, e.what(), context);
    return nullptr;
}
    yyjson_mut_obj_add_val(output_doc, output_root, "data", data_arr);
    yyjson_mut_obj_add_uint(output_doc, output_root, "count", count);
    if (stat->getErrorCode() == 0)
        yyjson_mut_obj_add_str(output_doc, output_root, "error", stat->getErrorMsg());
    else
        yyjson_mut_obj_add_null(output_doc, output_root, "error");
    yyjson_mut_obj_add_int(output_doc, output_root, "modified", stat->getChanges());
    char* body = yyjson_mut_write(output_doc, YYJSON_WRITE_ESCAPE_UNICODE, NULL);
    yyjson_mut_doc_free(output_doc);
    return body;
}

void bind_statement_bool(void* prepared_statement, const char* name, bool value)
{
    SQLite::Statement* stat = (SQLite::Statement*)prepared_statement;
    if (stat)
    {
        stat->bind(name, value);
    }
}

typedef bool (*request_body_handler_t)(void* userdata, void* json_object);

bool get_request_body(const char* body, int body_len, request_body_handler_t callback, void* userdata,
                      void* context)
{
    if (yyjson_doc* input_doc = yyjson_read(body, body_len, 0))
    {
        yyjson_val* input_root = yyjson_doc_get_root(input_doc);
        callback(userdata, input_root);
        yyjson_doc_free(input_doc);
        return true;
    }
    error_handler(__FUNCTION__, "failed to parse request body as json", context);
    return false;
}

typedef bool (*uri_parameter_handler_t)(const char* key, const char* value, void* userdata,
                                        void* context);

bool get_uri_params(const char* uri_text, uri_parameter_handler_t callback, void* userdata,
                    void* context)
{
    UriUriA uri;
    bool success = true;
    const char* error_pos;
    if (uriParseSingleUriA(&uri, uri_text, &error_pos) == URI_SUCCESS)
    {
        UriQueryListA* queryList;
        int itemCount;
        if (uriDissectQueryMallocA(&queryList, &itemCount, uri.query.first, uri.query.afterLast) == URI_SUCCESS)
        {
            for (auto i = queryList; i; i = i->next)
            {
                if (callback != nullptr)
                {
                    if (not callback(i->key, i->value, userdata, context))
                    {
                        success = false;
                        break;
                    }
                }
            }
            uriFreeQueryListA(queryList);
        }
        else
        {
            error_handler(__FUNCTION__, "failed to get list of parameters", context);
            success = false;
        }
        uriFreeUriMembersA(&uri);
    }
    else
    {
        error_handler(__FUNCTION__, "failed to parse uri", context);
        success = false;
    }
    return success;
}

inline std::string clip(const std::string& string, int max_len)
{
    if (string.length() > max_len)
        return string.substr(0, max_len) + "...";
    return string;
}

std::string get_error_page(const std::string& location, const std::string& description)
{
    return fmt::format(
        R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0"/>
    <title>Server Error</title>
    <link href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;500&family=IBM+Plex+Sans:wght@400;500;600&display=swap" rel="stylesheet"/>
{0}
</head>
<body>
<div class="card">
    <div class="header">
        <span class="icon">[!]</span>
        <span class="title">500: Server Error</span>
    </div>
    <hr class="divider"/>
    <div class="field">
        <div class="field-label">Location</div>
        <div class="field-value" id="location">{1}</div>
    </div>
    <div class="field">
        <div class="field-label">Description</div>
        <div class="field-value" id="description">{2}</div>
    </div>
</div>
</body>
</html>)",
        R"(<style>     *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }      :root {         --bg: #f4f4f4;         --surface: #ffffff;         --border: #d0d0d0;         --red: #b00020;         --text: #1a1a1a;         --muted: #666;         --sans: 'IBM Plex Sans', sans-serif;         --mono: 'IBM Plex Mono', monospace;     }      body {         background: var(--bg);         color: var(--text);         font-family: var(--sans);         min-height: 100vh;         display: grid;         place-items: center;         padding: 1rem;     }      .card {         background: var(--surface);         border: 1px solid var(--border);         border-left: 3px solid var(--red);         width: 100%;         max-width: 420px;         padding: 1.5rem;     }      .header {         display: flex;         align-items: center;         gap: .5rem;         margin-bottom: 1.25rem;     }      .icon {         font-size: .85rem;         color: var(--red);         font-family: var(--mono);         font-weight: 500;     }      .title {         font-size: .7rem;         font-weight: 600;         letter-spacing: .1em;         text-transform: uppercase;         color: var(--red);     }      .divider {         border: none;         border-top: 1px solid var(--border);         margin-bottom: 1.25rem;     }      .field { margin-bottom: 1rem; }     .field:last-child { margin-bottom: 0; }      .field-label {         font-size: .6rem;         font-weight: 600;         letter-spacing: .1em;         text-transform: uppercase;         color: var(--muted);         margin-bottom: .3rem;     }      .field-value {         font-family: var(--mono);         font-size: .75rem;         color: var(--text);         background: #fafafa;         border: 1px solid var(--border);         padding: .5rem .65rem;         line-height: 1.6;         word-break: break-word;     } </style>)",
        location,
        clip(description, 500));
}

#include <drogon/drogon.h>


namespace srv
{
    void init_listeners(const std::vector<application>& apps, const std::string& select_profile)
    {
        bool registered = false;
        for (const auto& app : apps)
        {
            for (const auto& profile : app.profile)
            {
                if (profile.name == select_profile or (select_profile.empty() and profile.default_))
                {
                    for (const auto& listen : profile.listen)
                    {
                        if (not registered) registered = true;
                        LOG(INFO) << fmt::format("profile:{} adding listener {}:{}", profile.name, listen.address,
                                                 listen.port);
                        drogon::app().addListener(listen.address, listen.port, not listen.cert.empty(), listen.cert,
                                                  listen.key);
                    }
                }
            }
        }
        if (not registered)
        {
            LOG(FATAL) << "no listener provided, please setup or select a profile";
        }
    }
}


using Callback = std::function<void (const drogon::HttpResponsePtr&)>;

void raise_unexpected_url_param_error(const char* where, const char* param, void* context)
{
    error_handler(where, fmt::format("unexpected url parameter \"{}\".", param).c_str(), context);
}

bool atob(const char* str)
{
    if (strcmp(str, "true") == 0 || strcmp(str, "1") == 0)
    {
        return true;
    }
    return false;
}

void error_handler(const char* what, const char* message, void* context)
{
    if (context)
    {
        drogon::HttpResponse* resp = (drogon::HttpResponse*)context;
        resp->setStatusCode(drogon::k400BadRequest);
        yyjson_mut_doc* output_doc = yyjson_mut_doc_new(NULL);
        yyjson_mut_val* output_root = yyjson_mut_obj(output_doc);
        yyjson_mut_doc_set_root(output_doc, output_root);
        yyjson_mut_obj_add_null(output_doc, output_root, "data");
        yyjson_mut_obj_add_uint(output_doc, output_root, "count", 0);
        yyjson_mut_val* error = yyjson_mut_obj_add_obj(output_doc, output_root, "error");
        yyjson_mut_obj_add_str(output_doc, error, "message", message);
        yyjson_mut_obj_add_str(output_doc, error, "location", what);
        yyjson_mut_obj_add_int(output_doc, output_root, "modified", 0);
        char* body = yyjson_mut_write(output_doc, YYJSON_WRITE_ESCAPE_UNICODE, NULL);
        if (false)
            resp->setBody(get_error_page(what, message));
        else
            resp->setBody(body);
        yyjson_mut_doc_free(output_doc);
        free(body);
    }
    else
    {
        LOG(ERROR) << what << ": " << message;
    }
}

typedef char* (*handler_t)(void* prepared_statement, const char* route, void* context, const char* body, int body_len);

char* handler_json_request(const char* route, void* context, const char* body, int body_len)
{
    if (yyjson_doc* input_doc = yyjson_read(body, body_len, 0))
    {
        yyjson_val* input_root = yyjson_doc_get_root(input_doc);
        int a = yyjson_get_int(yyjson_obj_get(input_root, "a"));
        int b = yyjson_get_int(yyjson_obj_get(input_root, "b"));
        yyjson_doc_free(input_doc);

        yyjson_mut_doc* output_doc = yyjson_mut_doc_new(NULL);
        yyjson_mut_val* output_root = yyjson_mut_obj(output_doc);
        yyjson_mut_doc_set_root(output_doc, output_root);
        yyjson_mut_obj_add_int(output_doc, output_root, "result", a + b);
        char* body = yyjson_mut_write(output_doc, YYJSON_WRITE_ESCAPE_UNICODE, NULL);
        yyjson_mut_doc_free(output_doc);
        return body;
    }
    error_handler(__FUNCTION__, "failed to parse JSON body.", context);
    return 0;
}

drogon::HttpMethod to_drogon_http_method(const std::string& method)
{
    if (method == "post")
        return drogon::Post;
    if (method == "put")
        return drogon::Put;
    if (method == "get")
        return drogon::Get;
    if (method == "delete")
        return drogon::Delete;
    if (method == "patch")
        return drogon::Patch;
    if (method == "options")
        return drogon::Options;
    return drogon::Invalid;
}

void log_message(const char* message)
{
    LOG(INFO) << message;
}

void log_address(unsigned long long message)
{
    LOG(INFO) << "Address: " << message;
}


namespace docs
{
    void init_docs(const std::vector<application>& apps, const std::vector<prepared_statement_metadata>& stats);
    unsigned char* get_docs_html();
}

namespace vws
{
    struct processed_template
    {
        std::vector<mch::node> nodes;
        std::string handler;
        std::string route;
        std::string method;
        void * prepared_statement {};
    };
    std::string read_file(const std::string& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (not file.is_open())
            throw std::runtime_error(fmt::format(R"(template file "{}" was not found)", path));
        const std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::string buffer(size, '\0');
        if (file.read(&buffer[0], size)) {
            return buffer;
        }
        return "";
    }
    void find_stmt_or_throw(const std::vector<generated_implementation>& queries, const std::string & entity_name, const std::string & query_name, const std::function<void(const generated_implementation&)> callback)
    {
        for (const auto& query : queries)
        {
            if (query.entity == entity_name and query.query_name == query_name)
            {
                callback(query);
                return;
            }
        }
        throw std::runtime_error(fmt::format("compiled query \"{}\" not found in entity \"{}\"", query_name, entity_name));
    };
    std::vector<processed_template> init_views(const std::vector<application>& apps, const std::vector<generated_implementation>& queries)
    {
        std::vector<processed_template> result;
        for (const auto & app : apps)
        {
            for (const auto & entity : app.entity)
            {
                for (const auto & template_ : entity.views.template_)
                {
                    std::string template_code;
                    if (template_.html.length() and template_.file.length())
                        throw std::runtime_error(fmt::format(R"(template for "{}" cannot have both html and file fields)", template_.name));
                    if (template_.html.empty() and template_.file.empty())
                        throw std::runtime_error(fmt::format(R"(template for "{}" must have an html or file specified)", template_.name));
                    if (template_.html.empty())
                        template_code = read_file(template_.file);
                    else template_code = template_.html;
                    if (not template_.query.empty())
                    {
                        find_stmt_or_throw(queries, misc::second_if_empty(template_.entity, entity.name), template_.query, [&result, &template_code, &template_](const generated_implementation&query)
                        {
                            result.push_back(processed_template{
                                .nodes = mch::parse(template_code),
                                .handler = query.name,
                                .route = template_.name,
                                .method = query.method,
                                .prepared_statement = query.prepared_statement,
                            });
                        });
                    } else
                    {
                        result.push_back(processed_template{
                               .nodes = mch::parse(template_code),
                               // .handler = query.name,
                               .route = template_.name,
                               .method = "get",
                               // .prepared_statement = query.prepared_statement,
                           });
                    }
                    // template_.
                }
            }
        }
        return result;
    }
}

namespace ath {
    SQLite::Statement get_login_statement(SQLite::Database & database, struct auth & auth){
        return SQLite::Statement(database, fmt::format("SELECT * FROM {}", auth.provider));
    }

    bool init_authentication(const std::vector<application>& apps){
        bool enabled = false;
        for(auto & app : apps){
            if(not app.auth.provider.empty()){
                enabled = true;
                auto database = db::get_database(app);
            }
        }
        return enabled;
    }
}

void server_mode(const std::string filename, const std::string profile)
{
    fmt::println(R"(┌┬┐┌─┐┬─┐┬─┐┌─┐┌┐┌┌─┐┬  ┬┌─┐
 │ ├┤ ├┬┘├┬┘├─┤││││ │└┐┌┘├─┤
 ┴ └─┘┴└─┴└─┴ ┴┘└┘└─┘ └┘ ┴ ┴)");
    auto file = std::ifstream(filename);
    if (!file.is_open())
        LOG(FATAL) << fmt::format("Could not open {}", filename);
    std::u8string content(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );

    file.close();
    // std::cout << std::string(reinterpret_cast<const char*>(content.data()),content.size()) << std::endl;
    std::optional<kdl::Document> document = kdl::parse(content);
    std::vector<application> apps;
    load(document.value(), apps);
    std::vector<prepared_statement_metadata> prepared_statements = db::init_statements(apps);
    docs::init_docs(apps, prepared_statements);
    auto init_result = service::init_services(prepared_statements);
    service::cjit service_layer("generated_service_layer_1.c");
    service_layer.push("log", (void*)log_message);
    service_layer.push("log_address", (void*)log_address);
    service_layer.push("duplicate_string", (void*)fake_strdup);
    service_layer.push("free", (void*)free);
    service_layer.push("collect_const_char", (void*)collect_const_char);
    service_layer.push("collect_bool", (void*)collect_bool);
    service_layer.push("collect_float", (void*)collect_float);
    service_layer.push("collect_int", (void*)collect_int);
    service_layer.push("prepared_statement_get_results_json", (void*)prepared_statement_get_results_json);
    service_layer.push("prepared_statement_reset", (void*)prepared_statement_reset);
    service_layer.push("bind_statement_const_char", (void*)bind_statement_const_char);
    service_layer.push("bind_statement_int", (void*)bind_statement_int);
    service_layer.push("bind_statement_float", (void*)bind_statement_float);
    service_layer.push("bind_statement_bool", (void*)bind_statement_bool);
    service_layer.push("raise_unexpected_url_param_error", (void*)raise_unexpected_url_param_error);
    service_layer.push("atoi", (void*)atoi);
    service_layer.push("atof", (void*)atof);
    service_layer.push("atob", (void*)atob);
    service_layer.push("strncmp", (void*)strncmp);
    service_layer.push("error_handler", (void*)error_handler);
    service_layer.push("get_uri_params", (void*)get_uri_params);
    service_layer.push("get_request_body", (void*)get_request_body);
    service_layer.push("prepared_statement_append_results_json", (void*)prepared_statement_append_results_json);
    service_layer.push("prepared_statement_finish_results_json", (void*)prepared_statement_finish_results_json);
    service_layer.compile(init_result.second);
    LOG(INFO) << fmt::format("generated & compiled {} api services successfully", init_result.first.size());
    for (generated_implementation& impl : init_result.first)
    {
        LOG(INFO) << fmt::format("{} {} -> {}(...)", impl.method, impl.route, impl.name);
        service_layer.push("get_request_body", (void*)get_request_body);
        handler_t handler = (handler_t)service_layer.peek(impl.name);
        void* prepared_stat = impl.prepared_statement;

        drogon::app().registerHandler(
            impl.route, [handler, prepared_stat](const drogon::HttpRequestPtr& req, Callback&& callback)
            {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setContentTypeCode(drogon::ContentType::CT_APPLICATION_JSON);
                if (auto _r0 = handler(prepared_stat, ("/?" + req->getQuery()).c_str(), (void*)resp.get(),
                                       req->getBody().data(), req->getBody().size()))
                {
                    resp->setBody(_r0);
                    free((void*)_r0);
                }
                callback(resp);
            }, {to_drogon_http_method(impl.method)});
    }
    auto processed_templates = vws::init_views(apps, init_result.first);
    mch::helper helper = mch::yyjson::make_yyjson_helper();
    auto ui_handlers = processed_templates.size();
    if (ui_handlers)
    {
        LOG(INFO) << fmt::format("generated & compiled {} ui services successfully", ui_handlers);
    }
    for (vws::processed_template& impl : processed_templates)
    {
        LOG(INFO) << fmt::format("{} {} -> {}(...)", impl.method, impl.route, impl.handler.empty() ? "opaque_handler" : impl.handler);
        service_layer.push("get_request_body", (void*)get_request_body);
        handler_t handler = impl.handler.empty() ? nullptr : (handler_t)service_layer.peek(impl.handler);
        void* prepared_stat = impl.prepared_statement;

        drogon::app().registerHandler(
            impl.route, [handler, prepared_stat, &helper, &impl](const drogon::HttpRequestPtr& req, Callback&& callback)
            {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setContentTypeCode(drogon::ContentType::CT_TEXT_HTML);
                if (handler)
                {
                    if (auto _r0 = handler(prepared_stat, ("/?" + req->getQuery()).c_str(), (void*)resp.get(),
                                       req->getBody().data(), req->getBody().size()))
                    {
                        resp->setBody(_r0);
                        free((void*)_r0);
                    }
                }
                const auto input = resp->getBody();
                yyjson_doc* doc = yyjson_read(input.data(), input.size(), 0);
                mch::yyjson::yyjson_render_context ctx(doc);
                resp->setBody(mch::render(impl.nodes, helper, &ctx));
                callback(resp);
            }, {to_drogon_http_method(impl.method)});
    }
    init_result.second.clear();
    document.reset();
    content.clear();
    srv::init_listeners(apps, profile);
    apps.clear();
    // hks::test();
    drogon::app()
        // .setServerHeaderField("logi/drogon"+drogon::getVersion())
        .run();
    service::free_all_tracked_malloc();
}

#ifndef TERRANOVA_VERSION
#define TERRANOVA_VERSION "<>"
#endif

int main(int argc, char** argv) try
{
    FLAGS_minloglevel = 0;
    FLAGS_logtostderr = true;
    FLAGS_stderrthreshold = 0;
    google::InitGoogleLogging(argv[0]);
    CLI::App app{"Terranova, a declarative language for defining REST APIs."};

    app.footer("v" TERRANOVA_VERSION "\n\"I wish you all the best!\"\n- Rene Descartes Muala, 2025.");

    bool version_flag = false;
    app.add_flag("-v,--version", version_flag, "Display version");

    auto* serve_cmd = app.add_subcommand("serve", "Start server");

    std::string file = "app.kdl";
    serve_cmd->add_option("-f,--file", file, "File to serve")
        ->capture_default_str(); // Shows default in help text

    std::string profile;
    serve_cmd->add_option("-p,--profile", profile, "Name of profile to use");

    serve_cmd->callback([&file, &profile]() {
        server_mode(file, profile);
    });

    /*
    auto* msgpack_cmd = app.add_subcommand("msgpack", "Test msgpack");

    msgpack_cmd->callback([]() {
        // Creating a client that connects to the localhost on port 8080
        rpc::client client("127.0.0.1", 8080);
        // Calling a function with paramters and converting the result to int
        auto result = client.call("add").as<int>();
        std::cout << "The result is: " << result << std::endl;
    });
    */

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        return app.exit(e);
    }

    if (version_flag) {
        std::cout << "v" TERRANOVA_VERSION << std::endl;
        return 0;
    }

    if (app.get_subcommands().empty()) {
        std::cout << app.help() << std::endl;
    }
    return 0;
}
catch (std::exception& e)
{
    LOG(ERROR) << e.what();
}
