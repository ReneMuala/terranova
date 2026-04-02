#include <any>
#include <fstream>
#include <iostream>
#include <regex>
#include <fmt/core.h>
#include "kdlpp.h"
#include <glog/logging.h>
#include "iguana/ylt/reflection/member_value.hpp"
#include <libtcc.h>
#include <yyjson.h>
#include <drogon/drogon_callbacks.h>
#include "uriparser/Uri.h"
#include "types.hpp"
#include <sqlite3.h>
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

inline std::string to_string(const std::u8string_view& str)
{
    return {reinterpret_cast<const char*>(str.data()), str.length()};
}

inline std::string tolower(std::string str)
{
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::tolower(c); });
    return str;
}

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

inline std::string to_route(const std::string& name, const bool with_prefix = true)
{
    return (with_prefix ? routes::current_namespace : std::string("/")) + std::regex_replace(
        tolower(name), std::regex("\\s"), "-");
}

inline std::string snake_to_kebab(const std::string& kebab)
{
    return std::regex_replace(kebab, std::regex("_"), "-");
}

inline std::string remove_trailing_underline(const std::string_view& name)
{
    return std::string(name.ends_with("_") ? name.substr(0, name.length() - 1) : name);
}

template <typename T>
inline std::string get_canonical_name()
{
    using namespace ylt::reflection;
    std::string cann_name = snake_to_kebab(fmt::format("{}(", ylt::reflection::get_struct_name<T>()));
    T type;
    bool fetched_fields = false;
    for_each(type, [&cann_name, &fetched_fields](auto& field, auto name, auto index)
    {
        std::string type_name;
        if constexpr (std::is_same_v<decltype(field), std::string&>)
            type_name = "string";
        else if constexpr (std::is_same_v<decltype(field), int&>)
            type_name = "int";
        else if constexpr (std::is_same_v<decltype(field), double&>)
            type_name = "double";
        else if constexpr (std::is_same_v<decltype(field), bool&>)
            type_name = "bool";
        auto fixed_name = snake_to_kebab(remove_trailing_underline(name));
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

template <typename T>
void load(kdl::Node& node, T& type)
{
    using namespace ylt::reflection;
    const auto struct_name = get_struct_name<T>();
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
        name = to_string(value.as<std::u8string>());
        if (name.empty()) throw std::runtime_error(fmt::format("\"{}\"'s name cannot be empty", struct_name));
    }
    for (const auto& prop : node.properties())
    {
        bool success = false;
        const auto prop_name = std::string(reinterpret_cast<const char*>(prop.first.data()), prop.first.length());
        for_each(type, [&](auto& field, auto name, auto index)
        {
            if (prop_name == snake_to_kebab(remove_trailing_underline(name)))
            {
                success = true;
                try
                {
                    if constexpr (std::is_same_v<decltype(field), std::string&>)
                        field = to_string(prop.second.as<std::u8string>());
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
                                                         snake_to_kebab(prop_name),
                                                         field_type,
                                                         to_string(prop.second.type())));
                }
            }
        });
        if (not success)
            throw std::runtime_error(fmt::format("property \"{}\" is not supported in \"{}\"",
                                                 snake_to_kebab(prop_name),
                                                 get_canonical_name<T>()));
    }
    for (auto& child : node.children())
    {
        bool success = false;
        const auto child_name = to_string(child.name());
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
            }
        });
        if (not success)
            throw std::runtime_error(
                fmt::format("child \"{}\" is not supported in \"{}\"", child_name, get_canonical_name<T>()));
    }
}

inline std::string toupper(std::string str)
{
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::toupper(c); });
    return str;
}

void load(kdl::Document& document, std::vector<application>& apps)
{
    for (auto& node : document.nodes())
    {
        if (node.name() == u8"application")
        {
            load<application>(node, apps.emplace_back());
        }
    }
}

namespace db
{
    inline const std::string& get_sql_type(const std::string& name)
    {
        static std::unordered_map<std::string, std::string> types{
            {"int", "INTEGER"},
            {"float", "REAL"},
            {"string", "TEXT"},
            {"blob", "BLOB"},
            {"file", "BLOB"},
            {"date", "DATE"},
            {"datetime", "DATETIME"},
            {"bool", "BOOL"},
        };
        const auto result = types.find(name);
        if (result != types.end())
        {
            return result->second;
        }
        {
            std::string error_msg = fmt::format("type \"{}\" is not supported. Available types: ", name);
            for (auto& type : types)
            {
                error_msg += fmt::format("{}({}), ", type.first, type.second);
            }
            error_msg.pop_back();
            error_msg.pop_back();
            throw std::runtime_error(error_msg);
        }
    }

    inline const std::string& get_c_type(const std::string& name)
    {
        static std::unordered_map<std::string, std::string> types{
            {"int", "int"},
            {"float", "float"},
            {"string", "const char *"},
            {"blob", "const char *"},
            {"file", "const char *"},
            {"date", "const char *"},
            {"datetime", "const char *"},
            {"bool", "bool"},
        };
        const auto result = types.find(name);
        if (result != types.end())
        {
            return result->second;
        }
        std::string error_msg = fmt::format("type \"{}\" is not supported. Available types: ", name);
        for (auto& type : types)
            error_msg += fmt::format("{}({}), ", type.first, type.second);
        error_msg.pop_back();
        error_msg.pop_back();
        throw std::runtime_error(error_msg);
    }

    inline std::string throw_if_invalid_identifier(const std::string& identifier)
    {
        if (std::regex_match(identifier, std::regex(R"(\w+)")))
            return identifier;
        throw std::runtime_error(fmt::format("\"{}\" is not a valid identifier", identifier));
    }

    inline const std::string& second_if_empty(const std::string& first, const std::string& second)
    {
        if (first.empty())
            return second;
        return first;
    }

    const field& get_field(const std::string& entity_name, const std::string& field_name,
                           const std::unordered_map<std::string, std::optional<std::reference_wrapper<const ::entity>>>&
                           em)
    {
        const auto it = em.find(entity_name);
        if (it == em.end() or not it->second)
            throw std::runtime_error(fmt::format("entity \"{}\" was not declared", entity_name));
        const auto entity = *it->second;
        for (auto& field : entity.get().schema.fields)
            if (tolower(field.name) == field_name) return field;
        if (auto& pk = entity.get().schema.pk)
            if (tolower(pk->name) == field_name) return (field&)*pk;
        throw std::runtime_error(fmt::format("field \"{}\" was not declared in \"{}\"", field_name, entity_name));
    }

    bool has_one_of_this(const std::string& other_entity_name, const std::string& this_entity_name,
                         const std::unordered_map<std::string, std::optional<std::reference_wrapper<const ::entity>>>&
                         em)
    {
        const auto it = em.find(other_entity_name);
        if (it == em.end() or not it->second)
            throw std::runtime_error(fmt::format("entity \"{}\" was not declared", other_entity_name));
        const auto entity = *it->second;
        for (auto& field : entity.get().schema.has_one)
        {
            if (field.name == this_entity_name) return true;
        }
        return false;
    }

    std::string get_on_delete_str(const std::string& other_entity_name, const std::string& this_entity_name,
                                  const std::unordered_map<
                                      std::string, std::optional<std::reference_wrapper<const ::entity>>>&
                                  em)
    {
        const auto it = em.find(other_entity_name);
        if (it == em.end() or not it->second)
            throw std::runtime_error(fmt::format("entity \"{}\" was not declared", other_entity_name));
        const auto entity = *it->second;
        for (auto& field : entity.get().schema.has_one)
        {
            if (field.name == this_entity_name) return field.on_delete;
        }
        for (auto& field : entity.get().schema.has_many)
        {
            if (field.name == this_entity_name) return field.on_delete;
        }
        return "CASCADE";
    }

    std::string get_on_update_str(const std::string& other_entity_name, const std::string& this_entity_name,
                                  const std::unordered_map<
                                      std::string, std::optional<std::reference_wrapper<const ::entity>>>&
                                  em)
    {
        const auto it = em.find(other_entity_name);
        if (it == em.end() or not it->second)
            throw std::runtime_error(fmt::format("entity \"{}\" was not declared", other_entity_name));
        const auto entity = *it->second;
        for (auto& field : entity.get().schema.has_one)
        {
            if (field.name == this_entity_name) return field.on_update;
        }
        for (auto& field : entity.get().schema.has_many)
        {
            if (field.name == this_entity_name) return field.on_update;
        }
        return "CASCADE";
    }

    inline std::string get_field_declaration(const std::string& name, const std::string& type, bool primary_key = false,
                                             bool unique = false, bool required = true)
    {
        std::string extras = primary_key ? " PRIMARY KEY" : "";
        if (not primary_key and unique)
            extras.append(" UNIQUE");
        if (required)
            extras.append(" NOT NULL");
        return fmt::format("{} {}{},", throw_if_invalid_identifier(tolower(name)),
                           get_sql_type(throw_if_invalid_identifier(type)), extras);
    }

    void init_entity(SQLite::Database& database, const entity& entity,
                     const std::unordered_map<std::string, std::optional<std::reference_wrapper<const ::entity>>>& em)
    {
        std::string stmt = fmt::format("CREATE TABLE IF NOT EXISTS {} (",
                                       throw_if_invalid_identifier(tolower(entity.name)));
        std::string fks;
        bool fields = false;
        if (entity.schema.pk)
            stmt.append(get_field_declaration(entity.schema.pk->name, entity.schema.pk->type, true));
        for (auto& field : entity.schema.fields)
        {
            if (not fields) fields = true;
            stmt.append(get_field_declaration(field.name, field.type, false, field.unique, not field.optional));
        }
        auto handle_relationship = [&](const auto& rel)
        {
            const std::string& it = throw_if_invalid_identifier(tolower(second_if_empty(rel.alias, rel.name)));
            if constexpr (std::is_same_v<decltype(rel), const belongs_to&>)
            {
                if (not fields) fields = true;
                const std::string target = throw_if_invalid_identifier(tolower(rel.name));
                const std::string target_id = throw_if_invalid_identifier(tolower(rel.on));
                auto& target_id_field = get_field(target, target_id, em);
                const bool is_unique = has_one_of_this(target, entity.name, em);
                const std::string on_delete_str = get_on_delete_str(target, entity.name, em);
                const std::string on_update_str = get_on_update_str(target, entity.name, em);
                stmt.append(get_field_declaration(fmt::format("{}_id", it), target_id_field.type, false, is_unique,
                                                  not rel.optional));
                fks.append(fmt::format("FOREIGN KEY({0}_id) REFERENCES {1}({2}) ON UPDATE {3} ON DELETE {4},", it,
                                       target, target_id, on_update_str, on_delete_str));
            }
        };
        for (auto& rel : entity.schema.has_one)
            handle_relationship(rel);
        for (auto& rel : entity.schema.has_many)
            handle_relationship(rel);
        for (auto& rel : entity.schema.belongs_to)
            handle_relationship(rel);
        if (fields)
            stmt.pop_back();
        if (not fks.empty())
        {
            fks = "," + fks;
            fks.pop_back();
        }

        stmt.append(fmt::format("{});", fks));
        LOG(INFO) << fmt::format("exec \"{}\"", stmt);
        database.exec(stmt);
    }

    prepared_statement_metadata init_stmt_select(const SQLite::Database& database, const entity& entity,
                                                 const std::unordered_map<
                                                     std::string, std::optional<std::reference_wrapper<const ::entity>>>
                                                 & em)
    {
        auto stmt = fmt::format("SELECT {0}.* FROM {0} LIMIT :limit OFFSET :offset;",
                                throw_if_invalid_identifier(tolower(entity.name)));
        LOG(INFO) << fmt::format("prepare statement \"{}\"", stmt);
        return prepared_statement_metadata{
            .entity = entity.name,
            .route = to_route(entity.name),
            .method = "get",
            .statement = SQLite::Statement(database, stmt),
            .params = {
                param{"limit", "int"},
                param{"offset", "int"},
            },
        };
    }

    inline std::string with_comma_suffix(const std::string& name)
    {
        return name + ",";
    }

    inline std::string with_comma_suffix_colon_prefix(const std::string& name)
    {
        return ":" + name + ",";
    }

    prepared_statement_metadata init_stmt_insert(const SQLite::Database& database, const entity& entity,
                                                 const std::unordered_map<
                                                     std::string, std::optional<std::reference_wrapper<const ::entity>>>
                                                 &
                                                 em)
    {
        std::string stmt = fmt::format("INSERT INTO {} ", throw_if_invalid_identifier(tolower(entity.name)));
        std::string values, value_fields;
        std::vector<struct param> params;
        bool fields = false;
        for (auto& field : entity.schema.fields)
        {
            if (not fields) fields = true;
            auto field_name = throw_if_invalid_identifier(tolower(field.name));
            values.append(with_comma_suffix(field_name));
            value_fields.append(with_comma_suffix_colon_prefix(field_name));
            params.emplace_back(field_name, get_c_type(field.type), field._comments);
        }
        auto handle_relationship = [&](const auto& rel)
        {
            const std::string& it = throw_if_invalid_identifier(tolower(second_if_empty(rel.alias, rel.name)));
            if constexpr (std::is_same_v<decltype(rel), const belongs_to&>)
            {
                if (not fields) fields = true;
                auto field_name = fmt::format("{}_id", it);
                values.append(with_comma_suffix(field_name));
                value_fields.append(with_comma_suffix_colon_prefix(field_name));
                auto& target_field = get_field(throw_if_invalid_identifier(tolower(rel.name)),
                                               throw_if_invalid_identifier(tolower(rel.on)), em);
                params.emplace_back(field_name, get_c_type(target_field.type),
                                    fmt::format("foreign key for {} ({})", rel.name,
                                                target_field.optional ? "optional" : "required"));
            }
        };
        for (auto& rel : entity.schema.has_one)
            handle_relationship(rel);
        for (auto& rel : entity.schema.has_many)
            handle_relationship(rel);
        for (auto& rel : entity.schema.belongs_to)
            handle_relationship(rel);
        if (fields)
            stmt.pop_back();
        if (not values.empty())
        {
            values.pop_back();
            value_fields.pop_back();
        }
        stmt = fmt::format("{} ({}) VALUES({});", stmt, values, value_fields);
        LOG(INFO) << fmt::format("prepare statement \"{}\"", stmt);
        return {
            .entity = entity.name,
            .route = to_route(entity.name),
            .method = "post",
            .statement = SQLite::Statement(database, stmt),
            .params = params,
            .data_provider = prepared_statement_metadata::request_body
        };
    }

    inline std::string form_set_statement(const std::string& name)
    {
        return fmt::format("{0} = :{0},", name);
    }

    prepared_statement_metadata init_stmt_update(const SQLite::Database& database, const entity& entity,
                                                 const std::unordered_map<
                                                     std::string, std::optional<std::reference_wrapper<const ::entity>>>
                                                 &
                                                 em)
    {
        std::vector<struct param> params;
        std::string sets;
        bool fields = false;
        for (auto& field : entity.schema.fields)
        {
            if (not fields) fields = true;
            auto field_name = throw_if_invalid_identifier(tolower(field.name));
            sets.append(form_set_statement(field_name));
            params.emplace_back(field_name, get_c_type(field.type), field._comments);
        }
        auto handle_relationship = [&](const auto& rel)
        {
            const std::string& it = throw_if_invalid_identifier(tolower(second_if_empty(rel.alias, rel.name)));
            if (not fields) fields = true;
            if constexpr (std::is_same_v<decltype(rel), const belongs_to&>)
            {
                auto field_name = throw_if_invalid_identifier(tolower(fmt::format("{}_id", it)));
                sets.append(form_set_statement(field_name));
                auto& target_field = get_field(throw_if_invalid_identifier(tolower(rel.name)),
                                               throw_if_invalid_identifier(tolower(rel.on)), em);
                params.emplace_back(field_name, get_c_type(target_field.type),
                                    fmt::format("foreign key for {} ({})", rel.name,
                                                target_field.optional ? "optional" : "required"));
            }
        };
        for (auto& rel : entity.schema.has_one)
            handle_relationship(rel);
        for (auto& rel : entity.schema.has_many)
            handle_relationship(rel);
        for (auto& rel : entity.schema.belongs_to)
            handle_relationship(rel);
        if (not sets.empty())
        {
            sets.pop_back();
        }
        std::string pk_name;
        if (entity.schema.pk)
        {
            pk_name = throw_if_invalid_identifier(tolower(entity.schema.pk->name));
            params.emplace_back(pk_name, get_c_type(entity.schema.pk->type));
        }
        std::string stmt = fmt::format("UPDATE {0} SET {1} WHERE {2} = :{2};",
                                       throw_if_invalid_identifier(tolower(entity.name)), sets, pk_name);
        LOG(INFO) << fmt::format("prepare statement \"{}\"", stmt);
        return {
            .entity = entity.name,
            .route = to_route(entity.name),
            .method = "put",
            .statement = SQLite::Statement(database, stmt),
            .params = params,
            .data_provider = prepared_statement_metadata::request_body
        };
    }

    prepared_statement_metadata init_stmt_delete(const SQLite::Database& database, const entity& entity,
                                                 const std::unordered_map<
                                                     std::string, std::optional<std::reference_wrapper<const ::entity>>>
                                                 &
                                                 em)
    {
        std::string pk_name;
        std::vector<struct param> params;
        if (entity.schema.pk)
        {
            pk_name = throw_if_invalid_identifier(tolower(entity.schema.pk->name));
            params.emplace_back(pk_name, get_c_type(entity.schema.pk->type), "required");
        }
        std::string stmt = fmt::format("DELETE FROM {0} WHERE {1} = :{1};",
                                       throw_if_invalid_identifier(tolower(entity.name)), pk_name);
        LOG(INFO) << fmt::format("prepare statement \"{}\"", stmt);
        return prepared_statement_metadata{
            .entity = entity.name,
            .route = to_route(entity.name),
            .method = "delete",
            .statement = SQLite::Statement(database, stmt),
            .params = params,
            .data_provider = prepared_statement_metadata::url_params
        };
    }

    bool contains(const std::string& target, const std::vector<param>& params)
    {
        std::string target_name = target.substr(1);
        for (auto& param : params)
        {
            if (param.name == target_name)
            {
                return true;
            }
        }
        return false;
    }

    void check_query_param_types(const std::vector<param>& params)
    {
        for (const auto& param : params)
        {
            get_sql_type(param.type);
        }
    }

    prepared_statement_metadata init_stmt_custom(const SQLite::Database& database, const entity& entity,
                                                 std::string name, std::string stmt,
                                                 const std::vector<param>& params,
                                                 const std::string http_method,
                                                 const std::string& comments,
                                                 const prepared_statement_metadata::data_provider_t data_provider_type =
                                                     prepared_statement_metadata::url_params)
    {
        std::vector<param> stat_params;
        stmt = std::regex_replace(stmt, std::regex(R"(\{table\})"), entity.name);
        LOG(INFO) << fmt::format("prepare custom statement (\"{}\") \"{}\"", name, stmt);
        std::regex param_regex(R"(:\w+)");
        std::sregex_iterator begin = std::sregex_iterator(stmt.begin(), stmt.end(), param_regex);
        std::sregex_iterator end;
        check_query_param_types(params);
        for (auto i = begin; i != end; ++i)
        {
            const std::string param = (*i).str();
            if (not contains(param, params))
                throw std::invalid_argument(fmt::format("query param \"{}\" is not specified", param));
        }
        for (const auto& param : params)
        {
            stat_params.emplace_back(throw_if_invalid_identifier(param.name), get_c_type(param.type), param._comments);
        }
        return prepared_statement_metadata{
            .entity = entity.name,
            .route = to_route(entity.name) + to_route(name, false),
            .method = http_method,
            .statement = SQLite::Statement(database, stmt),
            .params = stat_params,
            ._comments = comments
        };
    }

    void sql_custom_cap(sqlite3_context* ctx, int argc, sqlite3_value** argv)
    {
        const char* text = reinterpret_cast<const char*>(sqlite3_value_text(argv[0]));
        if (!text)
        {
            sqlite3_result_null(ctx);
            return;
        }
        std::string result(text);
        if (result.length())
        {
            result[0] = toupper(result[0]);
        }
        sqlite3_result_text(ctx, result.c_str(), -1, SQLITE_TRANSIENT);
    }

    void sql_custom_fetch(sqlite3_context* ctx, int argc, sqlite3_value** argv)
    {
        // Retrieve the Database object from user_data
        // int inputId = sqlite3_value_int(argv[0]);
        for (int i = 0 ; i < argc ; i++)
        {
            auto typ = sqlite3_value_type(argv[i]);
            std::cerr << fmt::format("{}. {}\n", i, typ);
        }
        //
        // try {
        sqlite3_result_null(ctx);
        // } catch (const SQLite::Exception& e) {
        //     sqlite3_result_error(ctx, e.what(), -1);
        // }
    }

    std::vector<prepared_statement_metadata> init_statements(const std::vector<application>& apps)
    {
        std::vector<prepared_statement_metadata> prepared_stmts;
        for (auto& app : apps)
        {
            routes::namespace_lock lock(app.namespace_);
            SQLite::Database database(fmt::format("{}.sqlite", throw_if_invalid_identifier(app.name)),
                                      SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
            database.createFunction("cap", 1, true, nullptr, sql_custom_cap, nullptr, nullptr);
            database.createFunction("fetch", -1, true, nullptr, sql_custom_fetch, nullptr, nullptr);
            std::unordered_map<std::string, std::optional<std::reference_wrapper<const entity>>> entity_ref_map;
            for (auto& entity : app.entity)
                entity_ref_map.emplace(throw_if_invalid_identifier(tolower(entity.name)), std::ref(entity));
            for (auto& entity : app.entity)
            {
                LOG(INFO) << fmt::format("generating queries for \"{}\"", entity.name);
                init_entity(database, entity, entity_ref_map);
                prepared_stmts.emplace_back(init_stmt_select(database, entity, entity_ref_map));
                prepared_stmts.emplace_back(init_stmt_insert(database, entity, entity_ref_map));
                if (entity.schema.pk)
                    prepared_stmts.emplace_back(init_stmt_update(database, entity, entity_ref_map));
                else
                    LOG(WARNING) << fmt::format("could not generate update statement for \"{}\", reason: no pk",
                                                entity.name);
                if (entity.schema.pk)
                    prepared_stmts.emplace_back(init_stmt_delete(database, entity, entity_ref_map));
                else
                    LOG(WARNING) << fmt::format("could not generate delete statement for \"{}\", reason: no pk",
                                                entity.name);
                for (const auto& query : entity.queries.get)
                    prepared_stmts.
                        emplace_back(init_stmt_custom(database, entity, query.name, query.sql, query.params, "get",
                                                      query._comments));
                for (const auto& query : entity.queries.post)
                    prepared_stmts.emplace_back(init_stmt_custom(database, entity, query.name, query.sql, query.params,
                                                                 "post", query._comments,
                                                                 prepared_statement_metadata::request_body));
                for (const auto& query : entity.queries.put)
                    prepared_stmts.emplace_back(init_stmt_custom(database, entity, query.name, query.sql, query.params,
                                                                 "put", query._comments,
                                                                 prepared_statement_metadata::request_body));
                for (const auto& query : entity.queries.delete_)
                    prepared_stmts.
                        emplace_back(init_stmt_custom(database, entity, query.name, query.sql, query.params, "delete",
                                                      query._comments));
            }
        }
        return std::move(prepared_stmts);
    }
}

namespace svc
{
    void error_handler(void* opaque, const char* msg)
    {
        const char* service_name = static_cast<char*>(opaque);
        LOG(WARNING) << fmt::format("tcc error: {} in service\"{}\"", msg, service_name);
        exit(0);
    }

    struct cjit
    {
        std::string name;
        TCCState* state = nullptr;
        cjit(const cjit&) = delete;
        cjit& operator=(const cjit&) = delete;
        cjit(cjit&&) = default;
        cjit& operator=(cjit&&) = default;

        explicit cjit(const std::string name) : name(name)
        {
            state = tcc_new();
            // tcc_set_lib_path(state, ".");
            tcc_set_options(state, "-nostdlib -Wall -Werror -bt 10");
            tcc_set_output_type(state, TCC_OUTPUT_MEMORY);
            tcc_set_error_func(state, (void*)name.data(), error_handler);
        }

        void compile(const std::string& code)
        {
            const auto result = tcc_compile_string(state, code.c_str());
            if (result != 0)
                throw std::runtime_error(fmt::format("load failed on service: \"{}\"", code));
            tcc_relocate(state, TCC_RELOCATE_AUTO);
        }

        void* peek(const std::string& name) const
        {
            if (state)
            {
                void* result = tcc_get_symbol(state, name.c_str());
                if (not result)
                    throw std::runtime_error(fmt::format("symbol not found: \"{}\"", name));
                return result;
            }
            return nullptr;
        }

        void push(const std::string& name, void* value)
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

    // std::function<void()> init_read_service(SQLite::Statement & statement)
    // {
    //     return [&statement]
    //     {
    //         statement;
    //
    //     };
    // }

    generated_implementation generate_implementation(const prepared_statement_metadata& stat)
    {
        static int id = 0;
        id++;
        std::string struct_def;
        std::string uri_param_handler_def;
        std::string output_handler_def;
        std::string request_body_handler_def;
        std::string destructor, constructor;
        std::string prepared_statement_usage;
        const std::string prefix = fmt::format(R"(
//----------BEGIN {0}: {3} "{2}" --------
/* {1} */
)", id, std::regex_replace(stat.statement.getQuery(), std::regex("\\*/"), "* /"), stat.route, stat.method);
        const std::string suffix = fmt::format(R"(
//----------END {0}--------
)", id);

        prepared_statement_usage = fmt::format("prepared_statement_reset(handler_{0}_prepared_statement);", id);
        if (not stat.params.empty())
        {
            struct_def = fmt::format("struct _{0}", id);
            if (stat.data_provider == prepared_statement_metadata::url_params)
                uri_param_handler_def = fmt::format(
                    "bool uri_parameter_handler_{0}(const char* key, const char* value, void* destination, void* context)",
                    id) + "{" + fmt::format("struct _{0}* params = (struct _{0}*)(destination);", id);
            else if (stat.data_provider == prepared_statement_metadata::request_body)
                request_body_handler_def = fmt::format(
                    "void request_body_handler_{0}(void* ob, void* ib)",
                    id) + "{" + fmt::format("struct _{0}* params = (struct _{0}*)(ob);", id);
            struct_def += "{";
            bool first = true;
            for (const auto& param : stat.params)
            {
                if (stat.data_provider == prepared_statement_metadata::url_params)
                    uri_param_handler_def += (first ? "" : "else ");
                constructor += fmt::format("input.{0} = 0;", param.name);
                if (param.type == "const char *")
                {
                    prepared_statement_usage += fmt::format(
                        "bind_statement_const_char(handler_{0}_prepared_statement, \":{1}\", input.{1});", id,
                        param.name);
                    if (stat.data_provider == prepared_statement_metadata::url_params)
                        uri_param_handler_def += fmt::format(
                            R"(if (strncmp(key, "{0}", {1}) == 0) {{  params->{0} = duplicate_string(value); /* {2} */ }})",
                            param.name,
                            param.name.size(), param.type);
                    else if (stat.data_provider == prepared_statement_metadata::request_body)
                        request_body_handler_def += fmt::format(
                            R"(collect_const_char("{0}", (void**)&params->{0}, ib);)", param.name);
                    destructor = fmt::format("if(input.{0})free((void*)input.{0});", param.name);
                }
                else if (param.type == "int")
                {
                    prepared_statement_usage += fmt::format(
                        "bind_statement_int(handler_{0}_prepared_statement, \":{1}\", input.{1});", id,
                        param.name);
                    if (stat.data_provider == prepared_statement_metadata::url_params)
                        uri_param_handler_def += fmt::format(
                            R"(if (strncmp(key, "{0}", {1}) == 0) params->{0} = atoi(value); /* {2} */)", param.name,
                            param.name.size(), param.type);
                    else if (stat.data_provider == prepared_statement_metadata::request_body)
                        request_body_handler_def += fmt::format(R"(collect_int("{0}", (void*)&params->{0}, ib);)",
                                                                param.name);
                }
                else if (param.type == "float")
                {
                    prepared_statement_usage += fmt::format(
                        "bind_statement_float(handler_{0}_prepared_statement, \":{1}\", input.{1});", id,
                        param.name);
                    if (stat.data_provider == prepared_statement_metadata::url_params)
                        uri_param_handler_def += fmt::format(
                            R"(if (strncmp(key, "{0}", {1}) == 0) params->{0} = (float)atof(value); /* {2} */)",
                            param.name, param.name.size(), param.type);
                    else if (stat.data_provider == prepared_statement_metadata::request_body)
                        request_body_handler_def += fmt::format(R"(collect_float("{0}", (void*)&params->{0}, ib);)",
                                                                param.name);
                }
                else if (param.type == "bool")
                {
                    prepared_statement_usage += fmt::format(
                        "bind_statement_bool(handler_{0}_prepared_statement, \":{1}\", input.{1});", id,
                        param.name);
                    if (stat.data_provider == prepared_statement_metadata::url_params)
                        uri_param_handler_def += fmt::format(
                            R"(if (strncmp(key, "{0}", {1}) == 0) params->{0} = atob(value); /* {2} */)", param.name,
                            param.name.size(), param.type);
                    else if (stat.data_provider == prepared_statement_metadata::request_body)
                        request_body_handler_def += fmt::format(R"(collect_bool("{0}", (void*)&params->{0}, ib);)",
                                                                param.name);
                }
                else
                    throw std::runtime_error(fmt::format(
                        "type \"{}\" is not supported in service implementation",
                        param.type));
                struct_def += fmt::format("{} {};", param.type, param.name);
                if (first) first = false;
            }
            if (stat.data_provider == prepared_statement_metadata::url_params)
                uri_param_handler_def +=
                    "else{raise_unexpected_url_param_error(__FUNCTION__, key, context); return false; } return true;}\n";
            else
                request_body_handler_def += "}";
            struct_def += "};\n";
        }
        prepared_statement_usage += fmt::format(
            "result = prepared_statement_get_results_json(handler_{0}_prepared_statement);", id);
        std::string body_def;
        if (stat.data_provider == prepared_statement_metadata::url_params and not stat.params.empty())
            body_def = fmt::format(
                "struct _{0} input;{1}if(get_uri_params(route, uri_parameter_handler_{0}, &input, context)){{ {2} return result; }}",
                id, constructor,
                prepared_statement_usage + destructor);
        else if (stat.data_provider == prepared_statement_metadata::request_body and not stat.params.empty())
            body_def = fmt::format(
                "struct _{0} input;{1}if(get_request_body(body, body_len, request_body_handler_{0}, &input, context)){{ {2} return result; }}",
                id, constructor,
                prepared_statement_usage + destructor);
        else
            body_def = constructor + prepared_statement_usage + destructor + "return result;";
        return generated_implementation{
            .entity = stat.entity,
            .name = fmt::format("handler_{0}", id),
            .route = stat.route,
            .method = stat.method,
            .code = prefix + struct_def + uri_param_handler_def + request_body_handler_def + fmt::format(
                "const char * handler_{0}(void * handler_{0}_prepared_statement, const char* route, void* context, const char* body, int body_len){{const char * result = 0;{1}return 0;}}",
                id, body_def) + suffix,
            .prepared_statement = (void*)&stat.statement
        };
    }

    std::pair<std::vector<generated_implementation>, std::string> init_services(
        std::vector<prepared_statement_metadata>& queries)
    {
        std::string code = R"(
typedef unsigned long long size_t;
typedef char bool;
#define true 1
#define false 0
void log(
    char const* _String);
void log_address(
    unsigned long long);
char* duplicate_string(
    char const* _String);
void free(
    void* _Block);
void collect_const_char(
    const char * name,
    void ** output_buffer,
    void * input_buffer);
void collect_bool(
    const char * name,
    void * output_buffer,
    void * input_buffer);
void collect_float(
    const char * name,
    void * output_buffer,
    void * input_buffer);
void collect_int(
    const char * name,
    void * output_buffer,
    void * input_buffer);
const char * prepared_statement_get_results_json(
    void *prepared_statement);
bool prepared_statement_reset(
    void * prepared_statement);
void bind_statement_const_char(
    void * prepared_statement,
    const char * name,
    const char* value);
void bind_statement_int(
    void * prepared_statement,
    const char * name,
    int value);
void bind_statement_float(
    void * prepared_statement,
    const char * name,
    float value);
void bind_statement_bool(
    void * prepared_statement,
    const char * name,
    bool value);
void raise_unexpected_url_param_error(
    const char * where,
    const char* param,
    void* context);
int  atoi(
    char const* _String);
double atof(
    char const* _String);
bool atob(
    const char* str);
int strncmp(
    char const* _Str1,
    char const* _Str2,
    size_t _MaxCount);
void error_handler(
    const char* what,
    const char* message,
    void* context);
typedef bool (*uri_parameter_handler_t)(
    const char* key,
    const char* value,
    void* destination,
    void* context);
bool get_uri_params(
    const char* uri_text,
    uri_parameter_handler_t callback,
    void* destination,
    void* context);
typedef void (*request_body_handler_t)(
    void* userdata,
    void* json_object);
bool get_request_body(
    const char* body,
    int body_len,
    request_body_handler_t callback,
    void* destination,
    void* context);
)";
        std::vector<generated_implementation> impls;
        // service svc1("default");
        for (auto& query : queries)
        {
            auto result = generate_implementation(query);
            code += result.code;
            result.code.clear();
            impls.emplace_back(result);
        }
        // LOG(INFO) << "// code: ";
        // std::cerr << code << std::endl;
        // return std::move(svc1)
        return std::make_pair<std::vector<generated_implementation>, std::string>(std::move(impls), std::move(code));
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

void collect_const_char(const char* name, void** output_buffer, void* input_buffer)
{
    if (name && output_buffer && input_buffer)
        *(char**)output_buffer = strdup(yyjson_get_str(yyjson_obj_get(static_cast<yyjson_val*>(input_buffer), name)));
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

const char* prepared_statement_get_results_json(void* prepared_statement)
{
    SQLite::Statement* stat = (SQLite::Statement*)prepared_statement;
    yyjson_mut_doc* output_doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val* output_root = yyjson_mut_obj(output_doc);
    yyjson_mut_doc_set_root(output_doc, output_root);
    yyjson_mut_val* data_arr = yyjson_mut_arr(output_doc);
    unsigned long long count = 0;
    int col_count = -1;
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

void error_handler(const char* what, const char* message, void* context);

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

struct req_params
{
    int a;
    int b;
};

void raise_unexpected_url_param_error(const char* where, const char* param, void* context)
{
    error_handler(where, fmt::format("unexpected url parameter \"{}\".", param).c_str(), context);
}

bool uri_parameter_handler_1(const char* key, const char* value, void* user_data, void* context)
{
    req_params* params = static_cast<req_params*>(user_data);
    if (strncmp(key, "a", 1) == 0)
    {
        params->a = std::stoi(value);
    }
    else if (strcmp(key, "b") == 0)
    {
        params->b = atoi(value);
    }
    else
    {
        raise_unexpected_url_param_error(__FUNCTION__, key, context);
        return false;
    }
    return true;
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

char* handler_route_params(const char* route, void* context)
{
    req_params params;
    if (get_uri_params(route, uri_parameter_handler_1, &params, context))
    {
        yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
        yyjson_mut_val* root = yyjson_mut_obj(doc);
        yyjson_mut_doc_set_root(doc, root);
        yyjson_mut_obj_add_int(doc, root, "result", params.a + params.b);
        char* body = yyjson_mut_write(doc, YYJSON_WRITE_ESCAPE_UNICODE, NULL);
        yyjson_mut_doc_free(doc);
        return body;
    }
    return 0;
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

int main(int argc, char** argv) try
{
    fmt::println(R"(┌┬┐┌─┐┬─┐┬─┐┌─┐┌┐┌┌─┐┬  ┬┌─┐
 │ ├┤ ├┬┘├┬┘├─┤││││ │└┐┌┘├─┤
 ┴ └─┘┴└─┴└─┴ ┴┘└┘└─┘ └┘ ┴ ┴)");
    FLAGS_minloglevel = 0;
    FLAGS_logtostderr = true;
    FLAGS_stderrthreshold = 0;
    google::InitGoogleLogging(argv[0]);
    const std::string filename = argc > 1 ? std::string(argv[1]) : std::string("app.kdl");
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
    auto init_result = svc::init_services(prepared_statements);
    svc::cjit service_layer("generated_service_layer_1.c");
    service_layer.push("log", (void*)log_message);
    service_layer.push("log_address", (void*)log_address);
    service_layer.push("duplicate_string", (void*)strdup);
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
    service_layer.compile(init_result.second);
    LOG(INFO) << fmt::format("generated & compiled {} services successfully", init_result.first.size());
    for (auto& impl : init_result.first)
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
    init_result.second.clear();
    document.reset();
    content.clear();
    srv::init_listeners(apps, argc > 2 ? std::string(argv[2]) : std::string());
    apps.clear();
    // hks::test();
    drogon::app()
        // .setServerHeaderField("logi/drogon"+drogon::getVersion())
        .run();
    return 0;
}
catch (std::exception& e)
{
    LOG(ERROR) << e.what();
}
