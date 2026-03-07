#include <any>
#include <fstream>
#include <regex>
#include <fmt/core.h>
#include "kdlpp.h"
#include <glog/logging.h>
#include "iguana/ylt/reflection/member_value.hpp"
#include <SQLiteCpp/SQLiteCpp.h>

std::unordered_map<std::string, std::any> handlers;

struct file_options
{
    std::string accept = "*";
    std::string folder = ".";
    int max_size = 1024 * 1024 * 10;
};

struct field
{
    std::string name;
    std::string type;
    std::optional<file_options> file_options;
};

struct has_many
{
    std::string name;
    std::string alias;
    std::string as;
    std::string on_delete = "cascade";
    std::string on = "id";
};

struct has_one
{
    std::string name;
    std::string alias;
    std::string as;
    std::string on_delete = "cascade";
    std::string on = "id";
};

struct belongs_to
{
    std::string name;
    std::string alias;
    std::string as;
    std::string on = "id";
};

struct pk
{
    std::string name;
    std::string type;
};

struct schema
{
    std::optional<pk> pk;
    std::vector<field> fields;
    std::vector<has_one> has_one;
    std::vector<has_many> has_many;
    std::vector<belongs_to> belongs_to;
};

struct before
{
    std::string name;
    std::string script;
    std::string fn;
};

struct after
{
    std::string name;
    std::string script;
    std::string fn;
};

struct hooks
{
    std::vector<before> before;
    std::vector<after> after;
};

struct param
{
    std::string name;
    std::string type;
};

struct get
{
    std::string name;
    std::string sql;
    std::vector<param> params;
};

struct post
{
    std::string name;
    std::string sql;
    std::vector<param> params;
};

struct put
{
    std::string name;
    std::string sql;
    std::vector<param> params;
};

struct delete_
{
    std::string name;
    std::string sql;
    std::vector<param> params;
};

struct queries
{
    std::vector<get> get;
    std::vector<post> post;
    std::vector<put> put;
    std::vector<delete_> delete_;
};

struct entity
{
    std::string name;
    schema schema;
    hooks hooks;
    queries queries;
};

struct auth
{
    std::string provider;
    std::string identity;
    std::string secret;
};

struct listen
{
    std::string address = "0.0.0.0";
    std::string cert;
    std::string key;
    int port = 80;
};

struct profile
{
    std::string name;
    bool default_ = false;
    std::vector<listen> listen;
};

struct application
{
    std::string name;
    std::string version = "1.0.0";
    auth auth;
    std::vector<entity> entity;
    std::vector<profile> profile;
};

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
                    throw std::runtime_error(fmt::format("property \"{}\" should be \"{}\" (not \"{}\")", snake_to_kebab(prop_name),
                                                         field_type,
                                                         to_string(prop.second.type())));
                }
            }
        });
        if (not success)
            throw std::runtime_error(fmt::format("property \"{}\" is not supported in \"{}\"", snake_to_kebab(prop_name),
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
        });
        if (not success)
            throw std::runtime_error(
                fmt::format("child \"{}\" is not supported in \"{}\"", child_name, get_canonical_name<T>()));
    }
}

inline std::string tolower(std::string str)
{
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::tolower(c); });
    return str;
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
        };
        const auto result = types.find(name);
        if (result != types.end())
        {
            return result->second;
        }
        {
            std::string error_msg = fmt::format("type \"{}\" is not supported. Available type: ", name);
            for (auto& type : types)
            {
                error_msg += fmt::format("{}({}), ", type.first, type.second);
            }
            error_msg.pop_back();
            error_msg.pop_back();
            throw std::runtime_error(error_msg);
        }
    }

    inline const std::string throw_if_invalid_identifier(const std::string identifier)
    {
        if (std::regex_match(identifier, std::regex(R"(\w+)")))
        {
            return identifier;
        }
        throw std::runtime_error(fmt::format("\"{}\" is not a valid identifier", identifier));
    }

    inline const std::string& second_if_empty(const std::string& first, const std::string& second)
    {
        if (first.empty())
            return second;
        return first;
    }

    const field & get_field(const std::string& entity_name, const std::string& field_name, const std::unordered_map<std::string, std::optional<std::reference_wrapper<const ::entity>>> & em)
    {
        const auto it = em.find(entity_name);
        if (it == em.end() or not it->second)
            throw std::runtime_error(fmt::format("entity \"{}\" was not declared", entity_name));
        const auto entity = *it->second;
        for (auto & field : entity.get().schema.fields)
            if (tolower(field.name) == field_name) return field;
        if (auto & pk = entity.get().schema.pk)
            if (tolower(pk->name) == field_name) return (field&)*pk;
        throw std::runtime_error(fmt::format("field \"{}\" was not declared in \"{}\"", field_name, entity_name));
    }

    inline std::string get_field_declaration(const std::string & name, const std::string & type, bool primary_key = false)
    {
        const std::string extras = primary_key ? " PRIMARY KEY": "";
        return fmt::format("{} {}{},", throw_if_invalid_identifier(tolower(name)),
                                    get_sql_type(throw_if_invalid_identifier(type)), extras);
    }

    void init_entity(SQLite::Database& database, const entity& entity, const std::unordered_map<std::string, std::optional<std::reference_wrapper<const ::entity>>> & em)
    {
        std::string stmt = fmt::format("CREATE TABLE IF NOT EXISTS {} (", throw_if_invalid_identifier(tolower(entity.name)));
        bool fields = false;
        if (entity.schema.pk)
            stmt.append(get_field_declaration(entity.schema.pk->name, entity.schema.pk->type, true));
        for (auto& field : entity.schema.fields)
        {
            if (not fields) fields = true;
            stmt.append(get_field_declaration(field.name, field.type));
        }
        auto handle_relationship = [&](const auto& rel)
        {
            const std::string& it = throw_if_invalid_identifier(tolower(second_if_empty(rel.alias, rel.name)));
            const std::string target = throw_if_invalid_identifier(tolower(rel.name));
            const std::string target_id = throw_if_invalid_identifier(tolower(rel.on));
            if (not fields) fields = true;
            auto & target_id_field = get_field(target, target_id, em);
            if constexpr (std::is_same_v<decltype(rel), const belongs_to&>)
            {
                stmt.append(get_field_declaration(fmt::format("{}_id", it), target_id_field.type));
                stmt.append(fmt::format("FOREIGN KEY({0}_id) REFERENCES {1}({2}),", it, target, target_id));
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
        stmt.append(");");
        LOG(INFO) << fmt::format("exec \"{}\"", stmt);
        database.exec(stmt);
    }

    void init_stmt_select(SQLite::Database& database, const entity& entity, const std::unordered_map<std::string, std::optional<std::reference_wrapper<const ::entity>>> & em)
    {
        auto code = fmt::format("SELECT {0}.* FROM {0};", throw_if_invalid_identifier(tolower(entity.name)));
        SQLite::Statement stmt(database, code);
        LOG(INFO) << fmt::format("prepare statement \"{}\"", code);
    }

    void init_persistence(const std::vector<application>& apps)
    {
        for (auto& app : apps)
        {
            SQLite::Database database(fmt::format("{}.sqlite", throw_if_invalid_identifier(app.name)), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
            std::unordered_map<std::string, std::optional<std::reference_wrapper<const entity>>> entity_ref_map;
            for (auto& entity : app.entity)
                entity_ref_map[throw_if_invalid_identifier(tolower(entity.name))] = std::ref(entity);
            for (auto& entity : app.entity)
            {
                init_entity(database, entity, entity_ref_map);
                init_stmt_select(database, entity, entity_ref_map);
            }
        }
    }
}

int main(int argc, char** argv) try
{
    FLAGS_minloglevel = 0;
    FLAGS_logtostderr = true;
    FLAGS_stderrthreshold = 0;
    google::InitGoogleLogging(argv[0]);
    auto file = std::ifstream("simple.kdl");
    if (!file.is_open())
    {
        LOG(FATAL) << "Could not open simple.xml";
    }
    std::u8string content(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );

    file.close();
    // std::cout << std::string(reinterpret_cast<const char*>(content.data()),content.size()) << std::endl;
    auto document = kdl::parse(content);
    std::vector<application> apps;
    load(document, apps);
    db::init_persistence(apps);
    // SQLite::Database db("database.db", SQLite::OPEN_READWRITE|SQLite::OPEN_CREATE);
    // Begin transaction
    // SQLite::Transaction transaction(db);

    // db.exec("CREATE TABLE test (id INTEGER PRIMARY KEY, value TEXT)");

    // int nb = db.exec("INSERT INTO test VALUES (NULL, \"test\")");
    // std::cout << "INSERT INTO test VALUES (NULL, \"test\")\", returned " << nb << std::endl;

    // Commit transaction
    // transaction.commit();
    // if (const auto result = doc.load(file))
    // {
    //     const auto & node = doc.first_child();
    //     assert_valid(node.name() == std::string("blueprint"));
    //     auto bp = blueprint::blueprint::make(node);
    //     blueprint::blueprint::print(bp, std::cout);
    //     fmt::println("name: {}", doc.child("blueprint").attribute("name").value());
    // } else
    // {
    //     fmt::println("description: {}", result.description());
    //     fmt::println(stderr, "failed to open file");
    // }
    return 0;
}
catch (std::exception& e)
{
    LOG(ERROR) << e.what();
}
