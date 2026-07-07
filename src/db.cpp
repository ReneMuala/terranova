#include "db.hpp"

#include <ctime>
#include <exception>
#include <fmt/core.h>
#include <glog/logging.h>
#include <sqlite3.h>

#include <algorithm>
#include <regex>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "SQLiteCpp/Database.h"
#include "authentication.hpp"
#include "fmt/base.h"
#include "fmt/format.h"
#include "misc.hpp"
#include "routes.hpp"
#include "types.hpp"

namespace db {
inline std::vector<param> validate_and_get_stat_params(const std::vector<param> &params,
                                                       const std::string &stmt,
                                                       const std::string &name);
inline const std::string &get_sql_type(const std::string &name, const std::string &param_name) {
    static std::unordered_map<std::string, std::string> types{
        {"int", "INTEGER"}, {"float", "REAL"}, {"string", "TEXT"},       {"blob", "BLOB"},
        {"file", "BLOB"},   {"date", "DATE"},  {"datetime", "DATETIME"}, {"bool", "BOOL"},
    };
    const auto result = types.find(name);
    if (result != types.end() and not name.empty()) {
        return result->second;
    }
    std::string error_msg;
    {
        error_msg =
            name.empty()
                ? fmt::format("missing type for param \"{}\". Available types: ", param_name)
                : fmt::format("type \"{}\" of param \"{}\" is not supported. Available types: ",
                              name, param_name);
        for (auto &type : types) {
            error_msg += fmt::format("{}({}), ", type.first, type.second);
        }
        error_msg.pop_back();
        error_msg.pop_back();
    }
    throw std::runtime_error(error_msg);
}

inline const std::string &get_c_type(const std::string &name) {
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
    if (result != types.end()) {
        return result->second;
    }
    std::string error_msg = fmt::format("type \"{}\" is not supported. Available types: ", name);
    for (auto &type : types)
        error_msg += fmt::format("{}({}), ", type.first, type.second);
    error_msg.pop_back();
    error_msg.pop_back();
    throw std::runtime_error(error_msg);
}

const field &get_field(
    const std::string &entity_name, const std::string &field_name,
    const std::unordered_map<std::string, std::optional<std::reference_wrapper<const ::entity>>>
        &em) {
    const auto it = em.find(entity_name);
    if (it == em.end() or not it->second)
        throw std::runtime_error(fmt::format("entity \"{}\" was not declared", entity_name));
    const auto entity = *it->second;
    for (auto &field : entity.get().schema.fields)
        if (misc::tolower(field.name) == field_name)
            return field;
    if (auto &pk = entity.get().schema.pk)
        if (misc::tolower(pk->name) == field_name)
            return (field &)*pk;
    throw std::runtime_error(
        fmt::format("field \"{}\" was not declared in \"{}\"", field_name, entity_name));
}

bool has_one_of_this(
    const std::string &other_entity_name, const std::string &this_entity_name,
    const std::unordered_map<std::string, std::optional<std::reference_wrapper<const ::entity>>>
        &em) {
    const auto it = em.find(other_entity_name);
    if (it == em.end() or not it->second)
        throw std::runtime_error(fmt::format("entity \"{}\" was not declared", other_entity_name));
    const auto entity = *it->second;
    for (auto &field : entity.get().schema.has_one) {
        if (field.name == this_entity_name)
            return true;
    }
    return false;
}

std::string get_on_delete_str(
    const std::string &other_entity_name, const std::string &this_entity_name,
    const std::unordered_map<std::string, std::optional<std::reference_wrapper<const ::entity>>>
        &em) {
    const auto it = em.find(other_entity_name);
    if (it == em.end() or not it->second)
        throw std::runtime_error(fmt::format("entity \"{}\" was not declared", other_entity_name));
    const auto entity = *it->second;
    for (auto &field : entity.get().schema.has_one) {
        if (field.name == this_entity_name)
            return field.on_delete;
    }
    for (auto &field : entity.get().schema.has_many) {
        if (field.name == this_entity_name)
            return field.on_delete;
    }
    return "CASCADE";
}

std::string get_on_update_str(
    const std::string &other_entity_name, const std::string &this_entity_name,
    const std::unordered_map<std::string, std::optional<std::reference_wrapper<const ::entity>>>
        &em) {
    const auto it = em.find(other_entity_name);
    if (it == em.end() or not it->second)
        throw std::runtime_error(fmt::format("entity \"{}\" was not declared", other_entity_name));
    const auto entity = *it->second;
    for (auto &field : entity.get().schema.has_one) {
        if (field.name == this_entity_name)
            return field.on_update;
    }
    for (auto &field : entity.get().schema.has_many) {
        if (field.name == this_entity_name)
            return field.on_update;
    }
    return "CASCADE";
}

inline std::string get_field_declaration(const std::string &name, const std::string &type,
                                         bool primary_key = false, bool unique = false,
                                         bool required = true) {
    std::string extras = primary_key ? " PRIMARY KEY" : "";
    if (not primary_key and unique)
        extras.append(" UNIQUE");
    if (required)
        extras.append(" NOT NULL");
    return fmt::format("{} {}{},", misc::throw_if_invalid_identifier(misc::tolower(name)),
                       get_sql_type(type, name), extras);
}

bool init_entity(
    SQLite::Database &database, const entity &entity,
    const std::unordered_map<std::string, std::optional<std::reference_wrapper<const ::entity>>>
        &em) {
    std::string stmt = fmt::format("CREATE TABLE IF NOT EXISTS {} (",
                                   misc::throw_if_invalid_identifier(misc::tolower(entity.name)));
    std::string fks;
    bool fields = false;
    if (entity.schema.pk)
        stmt.append(get_field_declaration(misc::throw_if_invalid_identifier(entity.schema.pk->name),
                                          entity.schema.pk->type, true));
    for (auto &field : entity.schema.fields) {
        if (not fields)
            fields = true;
        stmt.append(
            get_field_declaration(field.name, field.type, false, field.unique, not field.optional));
    }
    auto handle_relationship = [&](const auto &rel) {
        const std::string &it = misc::throw_if_invalid_identifier(
            misc::tolower(misc::second_if_empty(rel.as, rel.name)));
        if constexpr (std::is_same_v<decltype(rel), const belongs_to &>) {
            if (not fields)
                fields = true;
            const std::string target = misc::throw_if_invalid_identifier(misc::tolower(rel.name));
            const std::string target_id = misc::throw_if_invalid_identifier(misc::tolower(rel.on));
            auto &target_id_field = get_field(target, target_id, em);
            const bool is_unique = has_one_of_this(target, entity.name, em);
            const std::string on_delete_str = get_on_delete_str(target, entity.name, em);
            const std::string on_update_str = get_on_update_str(target, entity.name, em);
            stmt.append(get_field_declaration(fmt::format("{}_id", it), target_id_field.type, false,
                                              is_unique, not rel.optional));
            fks.append(
                fmt::format("FOREIGN KEY({0}_id) REFERENCES {1}({2}) ON UPDATE {3} ON DELETE {4},",
                            it, target, target_id, on_update_str, on_delete_str));
        }
    };
    for (auto &rel : entity.schema.has_one)
        handle_relationship(rel);
    for (auto &rel : entity.schema.has_many)
        handle_relationship(rel);
    for (auto &rel : entity.schema.belongs_to)
        handle_relationship(rel);
    if (fields)
        stmt.pop_back();
    if (not fks.empty()) {
        fks = "," + fks;
        fks.pop_back();
    }
    if (fields) {
        stmt.append(fmt::format("{});", fks));
        LOG(INFO) << fmt::format("exec \"{}\"", stmt);
        database.exec(stmt);
    }
    return fields;
}

prepared_statement_metadata init_stmt_select(const SQLite::Database &database, const entity &entity,
                                             unsigned long long index) {
    auto stmt = fmt::format("SELECT {0}.* FROM {0} LIMIT :limit OFFSET :offset;",
                            misc::throw_if_invalid_identifier(misc::tolower(entity.name)));
    LOG(INFO) << fmt::format("prepare statement \"{}\"", stmt);
    return prepared_statement_metadata{.name = "read",
                                       .entity = entity.name,
                                       .route = misc::to_route(entity.name),
                                       .method = "get",
                                       .statement = SQLite::Statement(database, stmt),
                                       .params =
                                           {
                                               param{"limit", "int"},
                                               param{"offset", "int"},
                                           },
                                       .is_composed = false,
                                       .index = index,
                                       .access = entity.access};
}

prepared_statement_metadata init_stmt_login(const SQLite::Database &database, const entity &entity,
                                            unsigned long long index, const auth &auth) {
    auto stmt =
        fmt::format("SELECT {0}.* FROM {0} WHERE {1} = :identity AND {3}({2}) = :secret LIMIT 1;",
                    misc::throw_if_invalid_identifier(misc::tolower(entity.name)),
                    misc::throw_if_invalid_identifier(misc::tolower(auth.identity)),
                    misc::throw_if_invalid_identifier(misc::tolower(auth.secret)),
                    misc::second_if_empty(misc::tolower(auth.hash), "/* WARNING: NO HASH! */"));
    LOG(INFO) << fmt::format("prepare statement \"{}\"", stmt);
    return prepared_statement_metadata{.name = "login",
                                       .entity = entity.name,
                                       .route = misc::to_route("auth/login"),
                                       .method = "post",
                                       .statement = SQLite::Statement(database, stmt),
                                       .params =
                                           {
                                               param{"identity", get_c_type("string")},
                                               param{"secret", get_c_type("string")},
                                           },
                                       .data_provider = prepared_statement_metadata::request_body,
                                       .is_composed = false,
                                       .index = index,
                                       .kind = statement_kind_t::login};
}

prepared_statement_metadata init_stmt_logout(const SQLite::Database &database, const entity &entity,
                                             unsigned long long index, const auth &auth) {
    auto stmt = fmt::format("/*LOGOUT OPAQUE QUERY*/ SELECT 1;");
    LOG(INFO) << fmt::format("prepare statement \"{}\"", stmt);
    return prepared_statement_metadata{.name = "logout",
                                       .entity = entity.name,
                                       .route = misc::to_route("auth/logout"),
                                       .method = "post",
                                       .statement = SQLite::Statement(database, stmt),
                                       .params = {},
                                       .is_composed = false,
                                       .index = index,
                                       .access = "private",
                                       .kind = statement_kind_t::logout};
}

#define ROLE_THROW_WHEN_EMPTY(X, Y)                                                                \
    if (X.empty())                                                                                 \
    throw std::runtime_error(                                                                      \
        fmt::format("field \"{}\" cannot be empty because {} in role {}", #X, Y, role.name))

#define ROLE_THROW_WHEN_NOT_EMPTY(X, Y)                                                            \
    if (not X.empty())                                                                             \
    throw std::runtime_error(                                                                      \
        fmt::format("field \"{}\" must be empty because {} in role {}", #X, Y, role.name))

prepared_statement_metadata init_stmt_role(const SQLite::Database &database, const entity &entity,
                                           unsigned long long index, const struct auth &auth,
                                           const struct role &role) {
    try {
        constexpr auto reseved_param_name = "identity";
        for (auto &p : role.params) {
            if (p.name == reseved_param_name) {
                throw std::runtime_error(fmt::format(
                    "param name \"{}\" is reserved for internal use.", reseved_param_name));
            }
        }
        auto stmt =
            std::regex_replace(fmt::format("SELECT COUNT(*) AS owned FROM {{table}} WHERE /* "
                                           "role.where*/ {} AND {{table}}.{} = :identity LIMIT 1",
                                           role.where, auth.identity),
                               std::regex(R"(\{table\})"), entity.name);
        auto params = role.params;
        params.push_back(
            param{.name = "identity", .type = "string", .value = "session." + auth.identity});
        std::vector<param> stat_params = validate_and_get_stat_params(params, stmt, role.name);
        LOG(INFO) << fmt::format("prepare statement \"{}\"", stmt);
        return prepared_statement_metadata{.name = role.name,
                                           .entity = entity.name,
                                           .route = role.name,
                                           .method = "(role)",
                                           .statement = SQLite::Statement(database, stmt),
                                           .params = stat_params,
                                           .is_composed = false,
                                           .index = index,
                                           .access = "private",
                                           .kind = statement_kind_t::role};
    } catch (const std::exception &ex) {
        throw std::runtime_error(fmt::format("{} in role \"{}\"", ex.what(), role.name));
    }
}

inline std::string with_comma_suffix(const std::string &name) { return name + ","; }

inline std::string with_comma_suffix_colon_prefix(const std::string &name) {
    return ":" + name + ",";
}

prepared_statement_metadata init_stmt_insert(
    const SQLite::Database &database, const entity &entity,
    const std::unordered_map<std::string, std::optional<std::reference_wrapper<const ::entity>>>
        &em,
    unsigned long long index) {
    std::string stmt = fmt::format("INSERT INTO {} ",
                                   misc::throw_if_invalid_identifier(misc::tolower(entity.name)));
    std::string values, value_fields;
    std::vector<struct param> params;
    bool fields = false;
    for (auto &field : entity.schema.fields) {
        if (not fields)
            fields = true;
        auto field_name = misc::throw_if_invalid_identifier(misc::tolower(field.name));
        values.append(with_comma_suffix(field_name));
        value_fields.append(with_comma_suffix_colon_prefix(field_name));
        params.emplace_back(param{
            .name = field_name, .type = get_c_type(field.type), ._comments = field._comments});
    }
    auto handle_relationship = [&](const auto &rel) {
        const std::string &it = misc::throw_if_invalid_identifier(
            misc::tolower(misc::second_if_empty(rel.as, rel.name)));
        if constexpr (std::is_same_v<decltype(rel), const belongs_to &>) {
            if (not fields)
                fields = true;
            auto field_name = fmt::format("{}_id", it);
            values.append(with_comma_suffix(field_name));
            value_fields.append(with_comma_suffix_colon_prefix(field_name));
            auto &target_field =
                get_field(misc::throw_if_invalid_identifier(misc::tolower(rel.name)),
                          misc::throw_if_invalid_identifier(misc::tolower(rel.on)), em);
            params.emplace_back(
                param{.name = field_name,
                      .type = get_c_type(target_field.type),
                      ._comments = fmt::format("foreign key for {} ({})", rel.name,
                                               target_field.optional ? "optional" : "required")});
        }
    };
    for (auto &rel : entity.schema.has_one)
        handle_relationship(rel);
    for (auto &rel : entity.schema.has_many)
        handle_relationship(rel);
    for (auto &rel : entity.schema.belongs_to)
        handle_relationship(rel);
    if (fields)
        stmt.pop_back();
    if (not values.empty()) {
        values.pop_back();
        value_fields.pop_back();
    }
    stmt = fmt::format("{} ({}) VALUES({});", stmt, values, value_fields);
    LOG(INFO) << fmt::format("prepare statement \"{}\"", stmt);
    return {.name = "create",
            .entity = entity.name,
            .route = misc::to_route(entity.name),
            .method = "post",
            .statement = SQLite::Statement(database, stmt),
            .params = params,
            .data_provider = prepared_statement_metadata::request_body,
            .is_composed = false,
            .index = index,
            .access = entity.access};
}

inline std::string form_set_statement(const std::string &name) {
    return fmt::format("{0} = :{0},", name);
}

prepared_statement_metadata init_stmt_update(
    const SQLite::Database &database, const entity &entity,
    const std::unordered_map<std::string, std::optional<std::reference_wrapper<const ::entity>>>
        &em,
    unsigned long long index) {
    std::vector<struct param> params;
    std::string sets;
    bool fields = false;
    for (auto &field : entity.schema.fields) {
        if (not fields)
            fields = true;
        auto field_name = misc::throw_if_invalid_identifier(misc::tolower(field.name));
        sets.append(form_set_statement(field_name));
        params.emplace_back(param{
            .name = field_name, .type = get_c_type(field.type), ._comments = field._comments});
    }
    auto handle_relationship = [&](const auto &rel) {
        const std::string &it = misc::throw_if_invalid_identifier(
            misc::tolower(misc::second_if_empty(rel.as, rel.name)));
        if (not fields)
            fields = true;
        if constexpr (std::is_same_v<decltype(rel), const belongs_to &>) {
            auto field_name =
                misc::throw_if_invalid_identifier(misc::tolower(fmt::format("{}_id", it)));
            sets.append(form_set_statement(field_name));
            auto &target_field =
                get_field(misc::throw_if_invalid_identifier(misc::tolower(rel.name)),
                          misc::throw_if_invalid_identifier(misc::tolower(rel.on)), em);
            params.emplace_back(field_name, get_c_type(target_field.type), std::string(),
                                std::string(),
                                fmt::format("foreign key for {} ({})", rel.name,
                                            target_field.optional ? "optional" : "required"));
        }
    };
    for (auto &rel : entity.schema.has_one)
        handle_relationship(rel);
    for (auto &rel : entity.schema.has_many)
        handle_relationship(rel);
    for (auto &rel : entity.schema.belongs_to)
        handle_relationship(rel);
    if (not sets.empty()) {
        sets.pop_back();
    }
    std::string pk_name;
    if (entity.schema.pk) {
        pk_name = misc::throw_if_invalid_identifier(misc::tolower(entity.schema.pk->name));
        params.emplace_back(param{.name = pk_name, .type = get_c_type(entity.schema.pk->type)});
    }
    std::string stmt =
        fmt::format("UPDATE {0} SET {1} WHERE {2} = :{2};",
                    misc::throw_if_invalid_identifier(misc::tolower(entity.name)), sets, pk_name);
    LOG(INFO) << fmt::format("prepare statement \"{}\"", stmt);
    return {.name = "update",
            .entity = entity.name,
            .route = misc::to_route(entity.name),
            .method = "put",
            .statement = SQLite::Statement(database, stmt),
            .params = params,
            .data_provider = prepared_statement_metadata::request_body,
            .is_composed = false,
            .index = index,
            .access = entity.access};
}

prepared_statement_metadata init_stmt_delete(const SQLite::Database &database, const entity &entity,
                                             unsigned long long index) {
    std::string pk_name;
    std::vector<struct param> params;
    if (entity.schema.pk) {
        pk_name = misc::throw_if_invalid_identifier(misc::tolower(entity.schema.pk->name));
        params.emplace_back(param{
            .name = pk_name, .type = get_c_type(entity.schema.pk->type), ._comments = "required"});
    }
    std::string stmt =
        fmt::format("DELETE FROM {0} WHERE {1} = :{1};",
                    misc::throw_if_invalid_identifier(misc::tolower(entity.name)), pk_name);
    LOG(INFO) << fmt::format("prepare statement \"{}\"", stmt);
    return prepared_statement_metadata{.name = "delete",
                                       .entity = entity.name,
                                       .route = misc::to_route(entity.name),
                                       .method = "delete",
                                       .statement = SQLite::Statement(database, stmt),
                                       .params = params,
                                       .data_provider = prepared_statement_metadata::url_params,
                                       .is_composed = false,
                                       .index = index,
                                       .access = entity.access};
}

bool contains(const std::string &target, const std::vector<param> &params) {
    std::string target_name = target.substr(1);
    for (auto &param : params) {
        if (param.name == target_name) {
            return true;
        }
    }
    return false;
}

void check_query_param_types(const std::vector<param> &params) {
    for (const auto &param : params) {
        get_sql_type(param.type, param.name);
    }
}

inline void validate_custom_query_parameter(const param &param) {
    if (param.value.empty() and not param.default_.empty())
        throw std::runtime_error(
            fmt::format("param \"{}\" has \"default\" but no \"value\" specified", param.name));
    if (not param.value.empty() and
        not(param.value.starts_with("session.") or param.value.starts_with("role.")))
        throw std::runtime_error(
            fmt::format("param \"{}\" value \"{}\" is not supported (only session.* or role.*)",
                        param.name, param.value));
}

inline std::vector<param> validate_and_get_stat_params(const std::vector<param> &params,
                                                       const std::string &stmt,
                                                       const std::string &name) {
    std::vector<param> stat_params;
    std::regex param_regex(R"(:\w+)");
    std::sregex_iterator begin = std::sregex_iterator(stmt.begin(), stmt.end(), param_regex);
    std::sregex_iterator end;
    check_query_param_types(params);
    std::unordered_set<std::string> detected_params;
    for (auto i = begin; i != end; ++i) {
        const std::string param = (*i).str();
        if (not contains(param, params))
            throw std::invalid_argument(fmt::format("query param \"{}\" is not specified", param));
        detected_params.insert(param.substr(1));
    }
    for (const auto &it : params) {
        if (not detected_params.contains(it.name))
            throw std::runtime_error(
                fmt::format("param \"{}\" has no usage in query \"{}\"", it.name, name));
        validate_custom_query_parameter(it);
        stat_params.emplace_back(param{.name = misc::throw_if_invalid_identifier(it.name),
                                       .type = get_c_type(it.type),
                                       .value = it.value,
                                       .default_ = it.default_,
                                       ._comments = it._comments});
    }

    return stat_params;
}

prepared_statement_metadata
init_stmt_custom_sql(bool is_override, const SQLite::Database &database, const entity &entity,
                     const std::string &name, std::string stmt, const std::vector<param> &params,
                     const std::string &http_method, const std::string &comments,
                     const unsigned long long index,
                     const prepared_statement_metadata::data_provider_t data_provider_type =
                         prepared_statement_metadata::url_params) {
    stmt = std::regex_replace(stmt, std::regex(R"(\{table\})"), entity.name);
    std::vector<param> stat_params = validate_and_get_stat_params(params, stmt, name);
    return prepared_statement_metadata{.name = name,
                                       .entity = entity.name,
                                       .route = is_override ? misc::to_route(entity.name)
                                                            : misc::to_route(entity.name) +
                                                                  misc::to_route(name, false),
                                       .method = http_method,
                                       .statement = SQLite::Statement(database, stmt),
                                       .params = stat_params,
                                       ._comments = comments,
                                       .is_composed = false,
                                       .index = index,
                                       .access = entity.access};
}

prepared_statement_metadata
init_stmt_custom_composed(bool is_override, const SQLite::Database &database, const entity &entity,
                          const std::string &name, const std::vector<struct data> &data,
                          const std::vector<param> &params, const std::string &http_method,
                          const std::string &comments, const unsigned long long index,
                          const prepared_statement_metadata::data_provider_t data_provider_type =
                              prepared_statement_metadata::url_params) {
    std::vector<param> stat_params;
    LOG(INFO) << fmt::format("prepare custom statement (\"{}\") \"{}\"", name, "<data>");
    for (const auto &it : params) {
        validate_custom_query_parameter(it);
        stat_params.emplace_back(param{.name = misc::throw_if_invalid_identifier(it.name),
                                       .type = get_c_type(it.type),
                                       ._comments = it._comments});
        bool has_match = false;
        for (const auto &data_item : data) {
            if (has_match)
                break;
            for (const auto &bind : data_item.binds) {
                if (misc::second_if_empty(bind.from, bind.name) == it.name) {
                    has_match = true;
                    break;
                }
            }
        }
        if (not has_match)
            throw std::runtime_error(
                fmt::format("param \"{}\" has no usage in query \"{}\"", it.name, name));
    }
    return prepared_statement_metadata{
        .name = name,
        .entity = entity.name,
        .route = is_override ? misc::to_route(entity.name)
                             : misc::to_route(entity.name) + misc::to_route(name, false),
        .method = http_method,
        .statement = SQLite::Statement(database, "SELECT 1 as result"),
        .params = stat_params,
        ._comments = comments,
        .is_composed = true,
        .data = data,
        .index = index,
        .access = entity.access};
}

inline prepared_statement_metadata
init_stmt_custom(bool is_override, const SQLite::Database &database, const entity &entity,
                 const std::string &name, const std::string stmt, const std::vector<param> &params,
                 const std::vector<data> &data, const std::string &http_method,
                 const std::string &comments, const unsigned long long index,
                 const prepared_statement_metadata::data_provider_t data_provider_type =
                     prepared_statement_metadata::url_params) {
    if (not stmt.empty())
        return init_stmt_custom_sql(is_override, database, entity, name, stmt, params, http_method,
                                    comments, index);
    if (not data.empty())
        return init_stmt_custom_composed(is_override, database, entity, name, data, params,
                                         http_method, comments, index);
    throw std::runtime_error(
        fmt::format("\"{}\" query should a sql param xor a data children.", name));
}

void sql_custom_cap(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    const char *text = reinterpret_cast<const char *>(sqlite3_value_text(argv[0]));
    if (!text) {
        sqlite3_result_null(ctx);
        return;
    }
    std::string result(text);
    if (result.length()) {
        result[0] = ::toupper(result[0]);
    }
    sqlite3_result_text(ctx, result.c_str(), -1, SQLITE_TRANSIENT);
}

void sql_custom_fetch(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    // Retrieve the Database object from user_data
    // int inputId = sqlite3_value_int(argv[0]);
    for (int i = 0; i < argc; i++) {
        auto typ = sqlite3_value_type(argv[i]);
        fmt::println("{}. {}\n", i, typ);
    }
    //
    // try {
    sqlite3_result_null(ctx);
    // } catch (const SQLite::Exception& e) {
    //     sqlite3_result_error(ctx, e.what(), -1);
    // }
}

void sql_custom_timestamp(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    sqlite3_result_int(ctx, (int)time(nullptr));
}
// owns connections for the whole program lifetime
static std::unordered_map<std::string, SQLite::Database> g_databases;

SQLite::Database &get_database(const struct application &app) {
    auto name = misc::throw_if_invalid_identifier(app.name);
    auto it = g_databases.find(name);
    if (it == g_databases.end()) {
        it = g_databases
                 .emplace(name, SQLite::Database(fmt::format("{}.sqlite", name),
                                                 SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE))
                 .first;
    }
    return it->second;
}

template <typename Q>
inline bool is_overriden_check(const std::string &name, const std::vector<Q> queries) {
    for (const auto &query : queries) {
        if (query.name == name and query._override) {
            return true;
        }
    }
    return false;
}

bool is_query_overriden(const std::string &name, const std::string &method, const entity &e) {
    if (method == "get") {
        return is_overriden_check(name, e.queries.get);
    } else if (method == "post") {
        return is_overriden_check(name, e.queries.post);
    } else if (method == "put") {
        return is_overriden_check(name, e.queries.put);
    } else if (method == "delete") {
        return is_overriden_check(name, e.queries.delete_);
    } else {
        return false;
    }
}

std::tuple<std::vector<prepared_statement_metadata>, bool>
init_statements(const std::vector<application> &apps) try {
    std::vector<prepared_statement_metadata> general_stmts;
    bool has_auth_stmts = false;
    for (auto &app : apps) {
        routes::namespace_lock lock(app.namespace_);
        SQLite::Database &database = get_database(app);
        database.createFunction("cap", 1, true, nullptr, sql_custom_cap, nullptr, nullptr);
        database.createFunction("fetch", -1, true, nullptr, sql_custom_fetch, nullptr, nullptr);
        database.createFunction("timestamp", 0, true, nullptr, sql_custom_timestamp, nullptr,
                                nullptr);
        std::unordered_map<std::string, std::optional<std::reference_wrapper<const entity>>>
            entity_ref_map;
        for (auto &entity : app.entity)
            entity_ref_map.emplace(misc::throw_if_invalid_identifier(misc::tolower(entity.name)),
                                   std::ref(entity));
        for (auto &entity : app.entity) {
            LOG(INFO) << fmt::format("generating queries for \"{}\"", entity.name);
            if (init_entity(database, entity, entity_ref_map)) {
                if (is_query_overriden("read", "get", entity)) {
                    LOG(WARNING) << fmt::format(
                        "skipping default read statement for \"{}\", reason: override",
                        entity.name);
                } else {
                    general_stmts.emplace_back(
                        init_stmt_select(database, entity, entity._4x_padded_index));
                }

                if (is_query_overriden("create", "post", entity)) {
                    LOG(WARNING) << fmt::format(
                        "skipping default create statement for \"{}\", reason: override",
                        entity.name);
                } else {
                    general_stmts.emplace_back(init_stmt_insert(database, entity, entity_ref_map,
                                                                entity._4x_padded_index + 1));
                }

                if (is_query_overriden("update", "put", entity)) {
                    LOG(WARNING) << fmt::format(
                        "skipping default update statement for \"{}\", reason: override",
                        entity.name);
                } else {
                    if (entity.schema.pk)
                        general_stmts.emplace_back(init_stmt_update(
                            database, entity, entity_ref_map, entity._4x_padded_index + 2));
                    else
                        LOG(WARNING) << fmt::format(
                            "could not generate update statement for \"{}\", reason: no pk",
                            entity.name);
                }

                if (is_query_overriden("delete", "delete", entity)) {
                    LOG(WARNING) << fmt::format(
                        "skipping default delete statement for \"{}\", reason: override",
                        entity.name);
                } else {
                    if (entity.schema.pk)
                        general_stmts.emplace_back(
                            init_stmt_delete(database, entity, entity._4x_padded_index + 3));
                    else
                        LOG(WARNING) << fmt::format(
                            "could not generate delete statement for \"{}\", reason: no pk",
                            entity.name);
                }
            }
            for (const auto &query : entity.queries.get)
                general_stmts.emplace_back(init_stmt_custom(
                    query._override, database, entity, query.name, query.sql, query.params,
                    query.data, "get", query._comments, query._index));
            for (const auto &query : entity.queries.post)
                general_stmts.emplace_back(
                    init_stmt_custom(query._override, database, entity, query.name, query.sql,
                                     query.params, query.data, "post", query._comments,
                                     query._index, prepared_statement_metadata::request_body));
            for (const auto &query : entity.queries.put)
                general_stmts.emplace_back(
                    init_stmt_custom(query._override, database, entity, query.name, query.sql,
                                     query.params, query.data, "put", query._comments, query._index,
                                     prepared_statement_metadata::request_body));
            for (const auto &query : entity.queries.delete_)
                general_stmts.emplace_back(init_stmt_custom(
                    query._override, database, entity, query.name, query.sql, query.params,
                    query.data, "delete", query._comments, query._index));
        }
        if (not has_auth_stmts) {
            has_auth_stmts = authentication::init_auth(app, entity_ref_map, general_stmts);
        }
    }

    return std::make_tuple(std::move(general_stmts), has_auth_stmts);
} catch (std::exception &e) {
    throw std::runtime_error(fmt::format("query/role error: {}", e.what()));
}
} // namespace db
