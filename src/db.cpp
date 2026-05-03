#include <unordered_map>
#include <fmt/core.h>
#include <algorithm>
#include <regex>
#include <glog/logging.h>
#include "routes.hpp"
#include <unordered_set>
#include <sqlite3.h>
#include "db.hpp"
#include "misc.hpp"

namespace db
{


    inline const std::string &get_sql_type(const std::string &name)
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
            for (auto &type : types)
            {
                error_msg += fmt::format("{}({}), ", type.first, type.second);
            }
            error_msg.pop_back();
            error_msg.pop_back();
            throw std::runtime_error(error_msg);
        }
    }

    inline const std::string &get_c_type(const std::string &name)
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
        for (auto &type : types)
            error_msg += fmt::format("{}({}), ", type.first, type.second);
        error_msg.pop_back();
        error_msg.pop_back();
        throw std::runtime_error(error_msg);
    }

    const field &get_field(const std::string &entity_name, const std::string &field_name,
                           const std::unordered_map<std::string, std::optional<std::reference_wrapper<const ::entity>>> &
                               em)
    {
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
        throw std::runtime_error(fmt::format("field \"{}\" was not declared in \"{}\"", field_name, entity_name));
    }

    bool has_one_of_this(const std::string &other_entity_name, const std::string &this_entity_name,
                         const std::unordered_map<std::string, std::optional<std::reference_wrapper<const ::entity>>> &
                             em)
    {
        const auto it = em.find(other_entity_name);
        if (it == em.end() or not it->second)
            throw std::runtime_error(fmt::format("entity \"{}\" was not declared", other_entity_name));
        const auto entity = *it->second;
        for (auto &field : entity.get().schema.has_one)
        {
            if (field.name == this_entity_name)
                return true;
        }
        return false;
    }

    std::string get_on_delete_str(const std::string &other_entity_name, const std::string &this_entity_name,
                                  const std::unordered_map<
                                      std::string, std::optional<std::reference_wrapper<const ::entity>>> &
                                      em)
    {
        const auto it = em.find(other_entity_name);
        if (it == em.end() or not it->second)
            throw std::runtime_error(fmt::format("entity \"{}\" was not declared", other_entity_name));
        const auto entity = *it->second;
        for (auto &field : entity.get().schema.has_one)
        {
            if (field.name == this_entity_name)
                return field.on_delete;
        }
        for (auto &field : entity.get().schema.has_many)
        {
            if (field.name == this_entity_name)
                return field.on_delete;
        }
        return "CASCADE";
    }

    std::string get_on_update_str(const std::string &other_entity_name, const std::string &this_entity_name,
                                  const std::unordered_map<
                                      std::string, std::optional<std::reference_wrapper<const ::entity>>> &
                                      em)
    {
        const auto it = em.find(other_entity_name);
        if (it == em.end() or not it->second)
            throw std::runtime_error(fmt::format("entity \"{}\" was not declared", other_entity_name));
        const auto entity = *it->second;
        for (auto &field : entity.get().schema.has_one)
        {
            if (field.name == this_entity_name)
                return field.on_update;
        }
        for (auto &field : entity.get().schema.has_many)
        {
            if (field.name == this_entity_name)
                return field.on_update;
        }
        return "CASCADE";
    }

    inline std::string get_field_declaration(const std::string &name, const std::string &type, bool primary_key = false,
                                             bool unique = false, bool required = true)
    {
        std::string extras = primary_key ? " PRIMARY KEY" : "";
        if (not primary_key and unique)
            extras.append(" UNIQUE");
        if (required)
            extras.append(" NOT NULL");
        return fmt::format("{} {}{},", misc::throw_if_invalid_identifier(misc::tolower(name)),
                           get_sql_type(misc::throw_if_invalid_identifier(type)), extras);
    }

    bool init_entity(SQLite::Database &database, const entity &entity,
                     const std::unordered_map<std::string, std::optional<std::reference_wrapper<const ::entity>>> &em)
    {
        std::string stmt = fmt::format("CREATE TABLE IF NOT EXISTS {} (",
                                       misc::throw_if_invalid_identifier(misc::tolower(entity.name)));
        std::string fks;
        bool fields = false;
        if (entity.schema.pk)
            stmt.append(get_field_declaration(entity.schema.pk->name, entity.schema.pk->type, true));
        for (auto &field : entity.schema.fields)
        {
            if (not fields)
                fields = true;
            stmt.append(get_field_declaration(field.name, field.type, false, field.unique, not field.optional));
        }
        auto handle_relationship = [&](const auto &rel)
        {
            const std::string &it = misc::throw_if_invalid_identifier(misc::tolower(misc::second_if_empty(rel.as, rel.name)));
            if constexpr (std::is_same_v<decltype(rel), const belongs_to &>)
            {
                if (not fields)
                    fields = true;
                const std::string target = misc::throw_if_invalid_identifier(misc::tolower(rel.name));
                const std::string target_id = misc::throw_if_invalid_identifier(misc::tolower(rel.on));
                auto &target_id_field = get_field(target, target_id, em);
                const bool is_unique = has_one_of_this(target, entity.name, em);
                const std::string on_delete_str = get_on_delete_str(target, entity.name, em);
                const std::string on_update_str = get_on_update_str(target, entity.name, em);
                stmt.append(get_field_declaration(fmt::format("{}_id", it), target_id_field.type, false, is_unique,
                                                  not rel.optional));
                fks.append(fmt::format("FOREIGN KEY({0}_id) REFERENCES {1}({2}) ON UPDATE {3} ON DELETE {4},", it,
                                       target, target_id, on_update_str, on_delete_str));
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
        if (not fks.empty())
        {
            fks = "," + fks;
            fks.pop_back();
        }
        if (fields)
        {
            stmt.append(fmt::format("{});", fks));
            LOG(INFO) << fmt::format("exec \"{}\"", stmt);
            database.exec(stmt);
        }
        return fields;
    }

    prepared_statement_metadata init_stmt_select(const SQLite::Database &database, const entity &entity, unsigned long long index)
    {
        auto stmt = fmt::format("SELECT {0}.* FROM {0} LIMIT :limit OFFSET :offset;",
                                misc::throw_if_invalid_identifier(misc::tolower(entity.name)));
        LOG(INFO) << fmt::format("prepare statement \"{}\"", stmt);
        return prepared_statement_metadata{
            .name = "select",
            .entity = entity.name,
            .route = misc::to_route(entity.name),
            .method = "get",
            .statement = SQLite::Statement(database, stmt),
            .params = {
                param{"limit", "int"},
                param{"offset", "int"},
            },
            .is_composed = false,
            .index = index};
    }

    inline std::string with_comma_suffix(const std::string &name)
    {
        return name + ",";
    }

    inline std::string with_comma_suffix_colon_prefix(const std::string &name)
    {
        return ":" + name + ",";
    }

    prepared_statement_metadata init_stmt_insert(const SQLite::Database &database, const entity &entity,
                                                 const std::unordered_map<
                                                     std::string, std::optional<std::reference_wrapper<const ::entity>>>
                                                     &
                                                         em,
                                                 unsigned long long index)
    {
        std::string stmt = fmt::format("INSERT INTO {} ", misc::throw_if_invalid_identifier(misc::tolower(entity.name)));
        std::string values, value_fields;
        std::vector<struct param> params;
        bool fields = false;
        for (auto &field : entity.schema.fields)
        {
            if (not fields)
                fields = true;
            auto field_name = misc::throw_if_invalid_identifier(misc::tolower(field.name));
            values.append(with_comma_suffix(field_name));
            value_fields.append(with_comma_suffix_colon_prefix(field_name));
            params.emplace_back(field_name, get_c_type(field.type), field._comments);
        }
        auto handle_relationship = [&](const auto &rel)
        {
            const std::string &it = misc::throw_if_invalid_identifier(misc::tolower(misc::second_if_empty(rel.as, rel.name)));
            if constexpr (std::is_same_v<decltype(rel), const belongs_to &>)
            {
                if (not fields)
                    fields = true;
                auto field_name = fmt::format("{}_id", it);
                values.append(with_comma_suffix(field_name));
                value_fields.append(with_comma_suffix_colon_prefix(field_name));
                auto &target_field = get_field(misc::throw_if_invalid_identifier(misc::tolower(rel.name)),
                                               misc::throw_if_invalid_identifier(misc::tolower(rel.on)), em);
                params.emplace_back(field_name, get_c_type(target_field.type),
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
            .name = "insert",
            .entity = entity.name,
            .route = misc::to_route(entity.name),
            .method = "post",
            .statement = SQLite::Statement(database, stmt),
            .params = params,
            .data_provider = prepared_statement_metadata::request_body,
            .is_composed = false,
            .index = index};
    }

    inline std::string form_set_statement(const std::string &name)
    {
        return fmt::format("{0} = :{0},", name);
    }

    prepared_statement_metadata init_stmt_update(const SQLite::Database &database, const entity &entity,
                                                 const std::unordered_map<
                                                     std::string, std::optional<std::reference_wrapper<const ::entity>>>
                                                     &
                                                         em,
                                                 unsigned long long index)
    {
        std::vector<struct param> params;
        std::string sets;
        bool fields = false;
        for (auto &field : entity.schema.fields)
        {
            if (not fields)
                fields = true;
            auto field_name = misc::throw_if_invalid_identifier(misc::tolower(field.name));
            sets.append(form_set_statement(field_name));
            params.emplace_back(field_name, get_c_type(field.type), field._comments);
        }
        auto handle_relationship = [&](const auto &rel)
        {
            const std::string &it = misc::throw_if_invalid_identifier(misc::tolower(misc::second_if_empty(rel.as, rel.name)));
            if (not fields)
                fields = true;
            if constexpr (std::is_same_v<decltype(rel), const belongs_to &>)
            {
                auto field_name = misc::throw_if_invalid_identifier(misc::tolower(fmt::format("{}_id", it)));
                sets.append(form_set_statement(field_name));
                auto &target_field = get_field(misc::throw_if_invalid_identifier(misc::tolower(rel.name)),
                                               misc::throw_if_invalid_identifier(misc::tolower(rel.on)), em);
                params.emplace_back(field_name, get_c_type(target_field.type),
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
        if (not sets.empty())
        {
            sets.pop_back();
        }
        std::string pk_name;
        if (entity.schema.pk)
        {
            pk_name = misc::throw_if_invalid_identifier(misc::tolower(entity.schema.pk->name));
            params.emplace_back(pk_name, get_c_type(entity.schema.pk->type));
        }
        std::string stmt = fmt::format("UPDATE {0} SET {1} WHERE {2} = :{2};",
                                       misc::throw_if_invalid_identifier(misc::tolower(entity.name)), sets, pk_name);
        LOG(INFO) << fmt::format("prepare statement \"{}\"", stmt);
        return {
            .name = "update",
            .entity = entity.name,
            .route = misc::to_route(entity.name),
            .method = "put",
            .statement = SQLite::Statement(database, stmt),
            .params = params,
            .data_provider = prepared_statement_metadata::request_body,
            .is_composed = false,
            .index = index};
    }

    prepared_statement_metadata init_stmt_delete(const SQLite::Database &database, const entity &entity, unsigned long long index)
    {
        std::string pk_name;
        std::vector<struct param> params;
        if (entity.schema.pk)
        {
            pk_name = misc::throw_if_invalid_identifier(misc::tolower(entity.schema.pk->name));
            params.emplace_back(pk_name, get_c_type(entity.schema.pk->type), "required");
        }
        std::string stmt = fmt::format("DELETE FROM {0} WHERE {1} = :{1};",
                                       misc::throw_if_invalid_identifier(misc::tolower(entity.name)), pk_name);
        LOG(INFO) << fmt::format("prepare statement \"{}\"", stmt);
        return prepared_statement_metadata{
            .name = "delete",
            .entity = entity.name,
            .route = misc::to_route(entity.name),
            .method = "delete",
            .statement = SQLite::Statement(database, stmt),
            .params = params,
            .data_provider = prepared_statement_metadata::url_params,
            .is_composed = false,
            .index = index};
    }

    bool contains(const std::string &target, const std::vector<param> &params)
    {
        std::string target_name = target.substr(1);
        for (auto &param : params)
        {
            if (param.name == target_name)
            {
                return true;
            }
        }
        return false;
    }

    void check_query_param_types(const std::vector<param> &params)
    {
        for (const auto &param : params)
        {
            get_sql_type(param.type);
        }
    }

    prepared_statement_metadata init_stmt_custom_sql(const SQLite::Database &database, const entity &entity,
                                                     const std::string &name, std::string stmt,
                                                     const std::vector<param> &params,
                                                     const std::string &http_method,
                                                     const std::string &comments,
                                                     const unsigned long long index,
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
        std::unordered_set<std::string> detected_params;
        for (auto i = begin; i != end; ++i)
        {
            const std::string param = (*i).str();
            if (not contains(param, params))
                throw std::invalid_argument(fmt::format("query param \"{}\" is not specified", param));
            detected_params.insert(param.substr(1));
        }
        for (const auto &param : params)
        {
            if (not detected_params.contains(param.name))
                throw std::runtime_error(fmt::format("param \"{}\" has no usage in query \"{}\"", param.name, name));
            stat_params.emplace_back(misc::throw_if_invalid_identifier(param.name), get_c_type(param.type), param._comments);
        }
        return prepared_statement_metadata{
            .name = name,
            .entity = entity.name,
            .route = misc::to_route(entity.name) + misc::to_route(name, false),
            .method = http_method,
            .statement = SQLite::Statement(database, stmt),
            .params = stat_params,
            ._comments = comments,
            .is_composed = false,
            .index = index};
    }

    prepared_statement_metadata init_stmt_custom_composed(const SQLite::Database &database, const entity &entity,
                                                          const std::string &name, const std::vector<struct data> &data,
                                                          const std::vector<param> &params,
                                                          const std::string &http_method,
                                                          const std::string &comments,
                                                          const unsigned long long index,
                                                          const prepared_statement_metadata::data_provider_t data_provider_type =
                                                              prepared_statement_metadata::url_params)
    {
        std::vector<param> stat_params;
        LOG(INFO) << fmt::format("prepare custom statement (\"{}\") \"{}\"", name, "<data>");
        for (const auto &param : params)
        {
            stat_params.emplace_back(misc::throw_if_invalid_identifier(param.name), get_c_type(param.type), param._comments);
            bool has_match = false;
            for (const auto &data_item : data)
            {
                if (has_match)
                    break;
                for (const auto &bind : data_item.binds)
                {
                    if (misc::second_if_empty(bind.from, bind.name) == param.name)
                    {
                        has_match = true;
                        break;
                    }
                }
            }
            if (not has_match)
                throw std::runtime_error(fmt::format("param \"{}\" has no usage in query \"{}\"", param.name, name));
        }
        return prepared_statement_metadata{
            .name = name,
            .entity = entity.name,
            .route = misc::to_route(entity.name) + misc::to_route(name, false),
            .method = http_method,
            .statement = SQLite::Statement(database, "SELECT 1 as result"),
            .params = stat_params,
            ._comments = comments,
            .is_composed = true,
            .data = data,
            .index = index};
    }

    inline prepared_statement_metadata init_stmt_custom(const SQLite::Database &database, const entity &entity,
                                                        const std::string &name, const std::string stmt,
                                                        const std::vector<param> &params,
                                                        const std::vector<data> &data,
                                                        const std::string &http_method,
                                                        const std::string &comments,
                                                        const unsigned long long index,
                                                        const prepared_statement_metadata::data_provider_t data_provider_type =
                                                            prepared_statement_metadata::url_params)
    {
        if (not stmt.empty())
            return init_stmt_custom_sql(database, entity, name, stmt, params, http_method, comments, index);
        if (not data.empty())
            return init_stmt_custom_composed(database, entity, name, data, params, http_method, comments, index);
        throw std::runtime_error(fmt::format("\"{}\" query should a sql param xor a data children.", name));
    }

    void sql_custom_cap(sqlite3_context *ctx, int argc, sqlite3_value **argv)
    {
        const char *text = reinterpret_cast<const char *>(sqlite3_value_text(argv[0]));
        if (!text)
        {
            sqlite3_result_null(ctx);
            return;
        }
        std::string result(text);
        if (result.length())
        {
            result[0] = ::toupper(result[0]);
        }
        sqlite3_result_text(ctx, result.c_str(), -1, SQLITE_TRANSIENT);
    }

    void sql_custom_fetch(sqlite3_context *ctx, int argc, sqlite3_value **argv)
    {
        // Retrieve the Database object from user_data
        // int inputId = sqlite3_value_int(argv[0]);
        for (int i = 0; i < argc; i++)
        {
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

    void sql_custom_session(sqlite3_context *ctx, int argc, sqlite3_value **argv)
    {
        // Retrieve the Database object from user_data
        // int inputId = sqlite3_value_int(argv[0]);
        for (int i = 0; i < argc; i++)
        {
            auto typ = sqlite3_value_type(argv[i]);
            fmt::println("{}. {}\n", i, typ);
        }
        //
        // try {
        // sqlite3_result_null(ctx);
        sqlite3_result_text(ctx, "SECRET", -1, SQLITE_TRANSIENT);
        // } catch (const SQLite::Exception& e) {
        //     sqlite3_result_error(ctx, e.what(), -1);
        // }
    }

    SQLite::Database get_database(const struct application &app)
    {
        return SQLite::Database(fmt::format("{}.sqlite", misc::throw_if_invalid_identifier(app.name)),
                                SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    }

    std::vector<prepared_statement_metadata> init_statements(const std::vector<application> &apps)
    {
        std::vector<prepared_statement_metadata> prepared_stmts;
        for (auto &app : apps)
        {
            routes::namespace_lock lock(app.namespace_);
            SQLite::Database database = get_database(app);
            database.createFunction("cap", 1, true, nullptr, sql_custom_cap, nullptr, nullptr);
            database.createFunction("fetch", -1, true, nullptr, sql_custom_fetch, nullptr, nullptr);
            database.createFunction("session", -1, true, nullptr, sql_custom_session, nullptr, nullptr);
            std::unordered_map<std::string, std::optional<std::reference_wrapper<const entity>>> entity_ref_map;
            for (auto &entity : app.entity)
                entity_ref_map.emplace(misc::throw_if_invalid_identifier(misc::tolower(entity.name)), std::ref(entity));
            for (auto &entity : app.entity)
            {
                LOG(INFO) << fmt::format("generating queries for \"{}\"", entity.name);
                if (init_entity(database, entity, entity_ref_map))
                {
                    prepared_stmts.emplace_back(init_stmt_select(database, entity, entity._4x_padded_index));
                    prepared_stmts.emplace_back(init_stmt_insert(database, entity, entity_ref_map, entity._4x_padded_index + 1));
                    if (entity.schema.pk)
                        prepared_stmts.emplace_back(init_stmt_update(database, entity, entity_ref_map, entity._4x_padded_index + 2));
                    else
                        LOG(WARNING) << fmt::format("could not generate update statement for \"{}\", reason: no pk",
                                                    entity.name);
                    if (entity.schema.pk)
                        prepared_stmts.emplace_back(init_stmt_delete(database, entity, entity._4x_padded_index + 3));
                    else
                        LOG(WARNING) << fmt::format("could not generate delete statement for \"{}\", reason: no pk",
                                                    entity.name);
                }
                for (const auto &query : entity.queries.get)
                    prepared_stmts.emplace_back(init_stmt_custom(database, entity, query.name, query.sql, query.params, query.data, "get",
                                                                 query._comments, query._index));
                for (const auto &query : entity.queries.post)
                    prepared_stmts.emplace_back(init_stmt_custom(database, entity, query.name, query.sql, query.params, query.data,
                                                                 "post", query._comments, query._index,
                                                                 prepared_statement_metadata::request_body));
                for (const auto &query : entity.queries.put)
                    prepared_stmts.emplace_back(init_stmt_custom(database, entity, query.name, query.sql, query.params, query.data,
                                                                 "put", query._comments, query._index,
                                                                 prepared_statement_metadata::request_body));
                for (const auto &query : entity.queries.delete_)
                    prepared_stmts.emplace_back(init_stmt_custom(database, entity, query.name, query.sql, query.params, query.data, "delete",
                                                                 query._comments, query._index));
            }
        }
        return std::move(prepared_stmts);
    }
}
