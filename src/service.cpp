#include "service.hpp"
#include <cstdlib>
#include <glog/logging.h>
#include <functional>
#include <regex>
#include <unordered_set>
#include "misc.hpp"
namespace service
{
    static std::list<void*> _tracked_malloc_data;
    void * tracked_malloc(size_t size)
    {
        void * it  = malloc(size);
        _tracked_malloc_data.push_back(it);
        return it;
    }
    void free_all_tracked_malloc()
    {
        for (auto & it : _tracked_malloc_data)
        {
            free(it);
        }
    }
    void error_handler(void* opaque, const char* msg)
    {
        const char* service_name = static_cast<char*>(opaque);
        LOG(WARNING) << fmt::format("tcc error: {} in service\"{}\"", msg, service_name);
        exit(0);
    }

    // std::function<void()> init_read_service(SQLite::Statement & statement)
    // {
    //     return [&statement]
    //     {
    //         statement;
    //
    //     };
    // }

    generated_implementation generate_implementation(const prepared_statement_metadata& stat, const std::function<void(const std::string & en, const std::string & qn, const std::function<void(const prepared_statement_metadata&)>& cbk)>& find_stmt)
    {
        unsigned long long id = stat.index;
        std::string struct_def;
        std::string uri_param_handler_def;
        std::string output_handler_def;
        std::string request_body_handler_def;
        std::string destructor, constructor;
        std::string prepared_statement_usage;
        void ** prepared_stat_array = (void**)(stat.is_composed ? tracked_malloc(sizeof(void*) * stat.data.size()) : (void*)&stat.statement);
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
            "result = prepared_statement_get_results_json(handler_{0}_prepared_statement, context);", id);
        if (stat.is_composed)
        {
            // we don't need to use prepared statements in composed queries
            prepared_statement_usage.clear();
            prepared_statement_usage.append("void * result_obj = 0;void * result_obj_root = 0;");
            int nested_index = 0;
            std::unordered_set<std::string> data_item_names;
            for (const auto & data_item : stat.data)
            {
                if (data_item_names.contains(data_item.name))
                        throw std::runtime_error(fmt::format(R"(data item name "{}" cannot be repeated, in query "{}")", data_item.name, stat.name));
                data_item_names.insert(data_item.name);
                find_stmt(misc::throw_if_invalid_identifier(misc::second_if_empty(data_item.entity, stat.entity)), misc::second_if_empty(data_item.query, data_item.name), [&prepared_stat_array,&nested_index,&stat, &data_item, &prepared_statement_usage](const prepared_statement_metadata & target)
                {
                    prepared_statement_usage.append("{");
                    prepared_stat_array[nested_index] = (void*)&target.statement;
                    prepared_statement_usage.append(fmt::format(R"(void * handler_{}_prepared_statement = ((void**)handler_{}_prepared_statement)[{}];)", target.index, stat.index, nested_index++));
                    if (target.is_composed)
                        throw std::runtime_error(fmt::format(R"(composed query "{}" cannot be used inside of another composed query "{}")", target.name, stat.name));
                    if (data_item.binds.size() != target.params.size())
                        throw std::runtime_error(fmt::format(R"(data item "{}" in query "{}" has {} bindings than required for query "{}")", data_item.name, stat.name, data_item.binds.size() > target.params.size() ? "more" : "less", target.name));
                    prepared_statement_usage.append(fmt::format("prepared_statement_reset(handler_{0}_prepared_statement);", target.index));
                    for (const auto & target_param : target.params)
                    {
                        bool target_param_found = false;
                        for (const auto & bind : data_item.binds)
                        {
                            if (bind.name == target_param.name)
                            {
                                auto from = misc::second_if_empty(bind.from, bind.name);
                                std::optional<struct param> binding_parameter;
                                for (auto & stat_parameter : stat.params)
                                {
                                    if (stat_parameter.name == from)
                                    {
                                         binding_parameter = stat_parameter;
                                    }
                                }
                                if (not binding_parameter)
                                    throw std::runtime_error(fmt::format(R"(bind "{}" in data item "{}" didn't match any param in query "{}")", bind.name, data_item.name, stat.name));
                                if (binding_parameter->type != target_param.type)
                                    throw std::runtime_error(fmt::format(R"(type mismatch between binding "{}" in data item "{}" and the correspondent param in query "{}")", bind.name, data_item.name, target.name));
                                const std::string type = target_param.type == "const char *" ? "const_char" : target_param.type;
                                prepared_statement_usage += fmt::format(R"(bind_statement_{}(handler_{}_prepared_statement, ":{}", input.{});)", type, target.index, target_param.name, binding_parameter->name);
                                target_param_found = true;
                                break;
                            }
                        }
                        if (!target_param_found)
                        {
                            throw std::runtime_error(fmt::format(R"(param "{}" from query "{}" was not bound in data item "{}")", target_param.name, target.name, stat.name));
                        }
                    }
                    prepared_statement_usage.append(fmt::format(R"(prepared_statement_append_results_json(&result_obj, &result_obj_root,"{}",handler_{}_prepared_statement, context);)", data_item.name, target.index));
                    prepared_statement_usage += "}";
                });
            }
            prepared_statement_usage.append(R"(result = prepared_statement_finish_results_json(result_obj);)");
        }
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
            .prepared_statement = prepared_stat_array,
            .is_composed = stat.is_composed,
            .query_name = stat.name
        };
    }

     void find_stmt_or_throw(const std::vector<prepared_statement_metadata>& queries, const std::string & entity_name, const std::string & query_name, const std::function<void(const prepared_statement_metadata&)> callback)
    {
        for (const auto& query : queries)
        {
            if (query.entity == entity_name and query.name == query_name)
            {
                callback(query);
                return;
            }
        }
        throw std::runtime_error(fmt::format("query \"{}\" not found in entity \"{}\"", query_name, entity_name));
    };

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
    void *prepared_statement,
    void * context);
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
const char * prepared_statement_finish_results_json(
    void * result);
void prepared_statement_append_results_json(
    void ** result,
    void ** root_field,
    const char * section,
    void* prepared_statement,
    void * context);
)";
        std::vector<generated_implementation> impls;
        // service svc1("default");

        const std::function<void(const std::string & en, const std::string & qn, std::function<void(const prepared_statement_metadata&)> cbk)> find_stmt = [&queries](const std::string & en, const std::string & qn, const std::function<void(const prepared_statement_metadata&)> & cbk)
        {
            find_stmt_or_throw(queries, en, qn, cbk);
        };
        for (auto& query : queries)
        {
            auto result = generate_implementation(query, find_stmt);
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
