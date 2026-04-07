//
// Created by dte on 3/30/2026.
//
#include <string>
#include "types.hpp"
#include <fmt/core.h>
#include <unordered_map>
#include <fstream>
#include <drogon/drogon_callbacks.h>
#include <drogon/HttpAppFramework.h>
#include <glog/logging.h>

namespace docs
{
    std::string yaml_escape(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (unsigned char c : s)
        {
            switch (c)
            {
            case '"': out += "\\\"";
                break;
            case '\\': out += "\\\\";
                break;
            case '\n': out += "\\n";
                break;
            case '\r': out += "\\r";
                break;
            case '\t': out += "\\t";
                break;
            case '\b': out += "\\b";
                break;
            case '\f': out += "\\f";
                break;
            case '\0': out += "\\0";
                break;
            case '\a': out += "\\a";
                break;
            case '\v': out += "\\v";
                break;
            case 0x1B: out += "\\e";
                break;
            default:
                if (c < 0x20 || c == 0x7F)
                {
                    // other control characters as \xNN
                    char buf[5];
                    snprintf(buf, sizeof(buf), "\\x%02x", c);
                    out += buf;
                }
                else
                {
                    out += c;
                }
                break;
            }
        }
        return out;
    }

    std::string to_yaml_value(const std::string& s)
    {
        return "\"" + yaml_escape(s) + "\"";
    }


#define IDENT       "  "
#define IDENT2X     "    "
#define IDENT3X     "      "
#define IDENT4X     "        "
#define IDENT5X     "          "
#define IDENT6X     "            "
#define IDENT7X     "              "
#define IDENT8X     "                "
#define IDENT9X     "                  "
#define IDENT10X    "                    "
#define IDENT11X    "                      "
#define IDENT12X    "                        "

    inline std::string get_app_specs(const application& app)
    {
        return fmt::format(
            "openapi: 3.0.3\n"
            "info:\n"
            IDENT"title: {0}\n"
            IDENT"description: {1}\n"
            IDENT"version: {2}\n"
            IDENT"contact:\n"
            IDENT2X"email: {3}\n"
            IDENT"license:\n"
            IDENT2X"name: {4}\n",
            app.name, to_yaml_value(app._comments), app.version, app.email, app.license);
    }

    inline std::string get_servers(const application& app)
    {
        std::string servers = "servers:\n";
        for (auto& profile : app.profile)
        {
            for (auto& listen : profile.listen)
            {
                std::string url;
                if (listen.port == 80)
                    url = fmt::format("http://{}", listen.domain);
                else if (listen.port == 443)
                    url = fmt::format("https://{}", listen.domain);
                else if (listen.cert.length() and listen.key.length())
                    url = fmt::format("https://{}:{}", listen.domain, listen.port);
                else
                    url = fmt::format("http://{}:{}", listen.domain, listen.port);
                servers.append(fmt::format(
                    IDENT"- url: {}\n"
                    IDENT2X"description: {}\n"
                    , url,
                    to_yaml_value(listen._comments)));
            }
        }
        return servers;
    }

    inline std::string get_tags(const application& app)
    {
        std::string servers = "tags:\n";
        for (auto& entity : app.entity)
        {
            servers.append(fmt::format(
                IDENT"- name: {}\n"
                IDENT2X"description: {}\n"
                , entity.name, to_yaml_value(entity._comments.empty() ? entity._comments : "entity")));
        }
        return servers;
    }

    inline const std::string& get_json_type_from_c_type(const std::string& name)
    {
        static std::unordered_map<std::string, std::string> types{
            {"int", "number"},
            {"float", "number"},
            {"const char *", "string"},
            {"bool", "boolean"},
        };
        const auto result = types.find(name);
        if (result != types.end())
        {
            return result->second;
        }
        std::string error_msg = fmt::format("c type \"{}\" is not supported in json", name);
        throw std::runtime_error(error_msg);
    }

    inline std::string get_param(const param& param, bool query)
    {
        return fmt::format(
            IDENT4X"- name: {0}\n"
            IDENT5X"in: {1}\n"
            IDENT5X"schema: {{ type: {2} }}\n"
            IDENT5X"description: {3}\n", param.name, "query", get_json_type_from_c_type(param.type),
            to_yaml_value(param._comments));
    }

    inline std::string get_params(const prepared_statement_metadata& stat)
    {
        std::string parameters = IDENT3X"parameters:\n";
        for (auto& param : stat.params)
            parameters.append(get_param(param, stat.data_provider == prepared_statement_metadata::url_params));
        return parameters;
    }

    inline std::string get_schema(unsigned long long index, const std::vector<param>& params)
    {
        std::string result = fmt::format(IDENT2X"bdy{}:\n"
                                         IDENT3X"type: object\n"
                                         IDENT3X"properties:\n"
                                         , index);
        for (auto& param : params)
            result.append(fmt::format(IDENT4X"{0}: {{ type: {1}, description: {2} }}\n", param.name,
                                      get_json_type_from_c_type(param.type), to_yaml_value(param._comments)));
        return result;
    }

    inline std::string get_request_body(const prepared_statement_metadata& stat, std::string& schemas)
    {
        static unsigned long long index = 0;
        index++;
        std::string parameters = IDENT3X"requestBody:\n"
            IDENT4X"required: true\n"
            IDENT4X"content:\n"
            IDENT5X"application/json:\n";
        parameters += fmt::format(IDENT6X"schema:  {{ $ref: '#/components/schemas/bdy{}' }}\n", index);
        schemas.append(get_schema(index, stat.params));
        return parameters;
    }

    inline std::string get_sql_responses()
    {
        return IDENT3X"responses:\n"
            IDENT4X"'200':\n"
            IDENT5X"description: OK\n"
            IDENT5X"content:\n"
            IDENT6X"application/json:\n"
            IDENT7X"schema:\n"
            IDENT8X"type: object\n"
            IDENT8X"properties:\n"
            IDENT9X"data:\n"
            IDENT10X"type: array\n"
            IDENT9X"count:\n"
            IDENT10X"type: number\n"
            IDENT9X"error:\n"
            IDENT10X"type: null\n"
            IDENT9X"modified:\n"
            IDENT10X"type: number\n"
            IDENT4X"'400':\n"
            IDENT5X"description: Error\n"
            IDENT5X"content:\n"
            IDENT6X"application/json:\n"
            IDENT7X"schema:\n"
            IDENT8X"type: object\n"
            IDENT8X"properties:\n"
            IDENT9X"data:\n"
            IDENT10X"type: null\n"
            IDENT9X"count:\n"
            IDENT10X"type: number\n"
            IDENT9X"error:\n"
            IDENT10X"type: string\n"
            IDENT9X"modified:\n"
            IDENT10X"type: number\n";
    }

    inline std::string get_composed_responses(const std::vector<struct data>& data)
    {
        std::string responses = IDENT3X"responses:\n"
            IDENT4X"'200':\n"
            IDENT5X"description: OK\n"
            IDENT5X"content:\n"
            IDENT6X"application/json:\n"
            IDENT7X"schema:\n"
            IDENT8X"type: object\n"
            IDENT8X"properties:\n";
        for (auto& data_item : data)
        {
            responses.append(fmt::format(IDENT9X"{}:\n"
                                         IDENT10X"type: object\n"
                                         IDENT10X"properties:\n"
                                         IDENT11X"data:\n"
                                         IDENT12X"type: array\n"
                                         IDENT11X"count:\n"
                                         IDENT12X"type: number\n"
                                         IDENT11X"error:\n"
                                         IDENT12X"type: null\n"
                                         IDENT11X"modified:\n"
                                         IDENT12X"type: number\n", data_item.name));
        }

        responses.append(IDENT4X"'400':\n"
            IDENT5X"description: Error\n"
            IDENT5X"content:\n"
            IDENT6X"application/json:\n"
            IDENT7X"schema:\n"
            IDENT8X"type: object\n"
            IDENT8X"properties:\n"
            IDENT9X"data:\n"
            IDENT10X"type: null\n"
            IDENT9X"count:\n"
            IDENT10X"type: number\n"
            IDENT9X"error:\n"
            IDENT10X"type: string\n"
            IDENT9X"modified:\n"
            IDENT10X"type: number\n");

        return responses;
    }

    inline std::string get_path(const prepared_statement_metadata& stat, std::string& schemas)
    {
        return fmt::format(
            IDENT2X"{0}:\n" // method
            IDENT3X"tags: [{1}]\n" // entity
            IDENT3X"summary: {2}\n" // comments
            "{3}" // params
            "{4}" // responses
            , stat.method, stat.entity, to_yaml_value(stat._comments),
            stat.data_provider == prepared_statement_metadata::url_params
                ? get_params(stat)
                : get_request_body(stat, schemas),
            stat.is_composed ? get_composed_responses(stat.data) : get_sql_responses());
    }

    inline std::string get_paths(const std::vector<prepared_statement_metadata>& stats, std::string& schemas)
    {
        std::string paths = "paths:\n";
        std::unordered_map<std::string, std::string> umap;
        for (auto& stat : stats)
            umap[stat.route] += get_path(stat, schemas);
        for (auto& pair : umap)
            paths.append(fmt::format(IDENT"{}:\n{}", pair.first, pair.second));
        return paths;
    }

    using Callback = std::function<void (const drogon::HttpResponsePtr&)>;

    void docs_handler(const drogon::HttpRequestPtr& req, Callback&& callback)
    {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setContentTypeCode(drogon::ContentType::CT_TEXT_HTML);
        static unsigned char docs_html_data[] = {
#include "docs.html.h"
        };
        static const std::string docs_html = {reinterpret_cast<const char*>(docs_html_data)};
        resp->setBody(docs_html);
        callback(resp);
    }

    void init_docs(const std::vector<application>& apps, const std::vector<prepared_statement_metadata>& stats)
    {
        std::string filename;
        for (auto& app : apps)
        {
            if (not app.serve_docs) return;
            std::string schemas = "components:\n"
                IDENT"schemas:\n";
            std::string spec = get_app_specs(app) + get_servers(app) + get_tags(app) + get_paths(stats, schemas) +
                schemas;
            filename = fmt::format("{}.yaml", app.name);
            std::ofstream out(filename);
            out << spec;
            break;
        }
        LOG(INFO) << fmt::format("get /docs.html | disable with application serve-docs=false ...");
        drogon::app().registerHandler("/docs.html", [](const drogon::HttpRequestPtr& req, Callback&& callback)
        {
            docs_handler(req, std::move(callback));
        });

        LOG(INFO) << fmt::format("get /docs.yaml | disable with application serve-docs=false ...");
        drogon::app().registerHandler("/docs.yaml", [filename](const drogon::HttpRequestPtr& req, Callback&& callback)
        {
            callback(drogon::HttpResponse::newFileResponse(filename));
        });
    }
}
