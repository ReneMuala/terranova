//
// Created by dte on 3/30/2026.
//

#pragma once
#include <string>
#include <vector>
#include <optional>
#include <SQLiteCpp/SQLiteCpp.h>

struct file_options
{
    std::string accept = "*";
    std::string folder = ".";
    int max_size = 1024 * 1024 * 10;
    std::string _comments;
};

struct field
{
    std::string name;
    std::string type;
    bool optional = false;
    bool unique = false;
    std::optional<struct file_options> file_options;
    std::string _comments;
};

struct has_many
{
    std::string name;
    std::string as;
    std::string on_delete = "cascade";
    std::string on_update = "no action";
    bool optional = false;
    std::string _comments;
};

struct has_one
{
    std::string name;
    std::string as;
    std::string on_delete = "cascade";
    std::string on_update = "no action";
    bool optional = false;
    std::string _comments;
};

struct belongs_to
{
    std::string name;
    std::string as;
    std::string on = "id";
    bool optional = false;
    std::string _comments;
};

struct pk
{
    std::string name;
    std::string type;
    std::string _comments;
};

struct schema
{
    std::optional<struct pk> pk;
    std::vector<struct field> fields;
    std::vector<struct has_one> has_one;
    std::vector<struct has_many> has_many;
    std::vector<struct belongs_to> belongs_to;
    std::string _comments;
};

struct before
{
    std::string name;
    std::string script;
    std::string fn;
    std::string _comments;
};

struct after
{
    std::string name;
    std::string script;
    std::string fn;
    std::string _comments;
};

struct hooks
{
    std::vector<struct before> before;
    std::vector<struct after> after;
    std::string _comments;
};

struct param
{
    std::string name;
    std::string type;
    std::string _comments;
};

struct bind
{
    std::string name;
    std::string from;
    std::string _comments;
};

struct data
{
    std::string name;
    std::string query;
    std::string entity;
    std::vector<struct bind> binds;
    std::string _comments;
};

struct get
{
    std::string name;
    std::string sql;
    std::vector<struct param> params;
    std::vector<struct data> data;
    std::string _comments;
    unsigned long long _index;
};

struct post
{
    std::string name;
    std::string sql;
    std::vector<struct param> params;
    std::vector<struct data> data;
    std::string _comments;
    unsigned long long _index;
};

struct put
{
    std::string name;
    std::string sql;
    std::vector<struct param> params;
    std::vector<struct data> data;
    std::string _comments;
    unsigned long long _index;
};

struct delete_
{
    std::string name;
    std::string sql;
    std::vector<struct param> params;
    std::vector<struct data> data;
    std::string _comments;
    unsigned long long _index;
};

struct queries
{
    std::vector<struct get> get;
    std::vector<struct post> post;
    std::vector<struct put> put;
    std::vector<struct delete_> delete_;
    std::string _comments;
};

struct for_
{
    std::string name;
    std::string route;
    std::string query;
    std::string _comments;
};

struct template_set
{
    std::vector<struct for_> for_;
    std::string _comments;
};

struct template_
{
    std::string name;
    std::string query;
    std::string entity;
    std::string file;
    std::string html;
    std::string _comments;
};

struct views
{
    std::vector<struct template_> template_;
    // disable template_set for now
    // std::vector<struct template_set> template_set;
    std::string _comments;
};

struct entity
{
    std::string name;
    struct schema schema;
    bool protected_ = false;
    // struct hooks hooks; disable hooks for now
    struct queries queries;
    struct views views;
    std::string _comments;
    unsigned long long _4x_padded_index;
};

// internal, not specified via kdl
struct prepared_statement_metadata
{
    enum data_provider_t
    {
        url_params,
        request_body,
    };

    std::string name;
    std::string entity;
    std::string route;
    std::string method = "get";
    SQLite::Statement statement;
    std::vector<struct param> params;
    data_provider_t data_provider = url_params;
    std::string _comments;
    bool is_composed = true;
    std::vector<struct data> data;
    unsigned long long index;
};

// internal, not specified via kdl
struct generated_implementation
{
    std::string entity;
    std::string name;
    std::string route;
    std::string method;
    std::string code;
    void* prepared_statement{};
    bool is_composed = false;
    std::string query_name;
};

struct auth
{
    std::string provider;
    std::string identity;
    std::string secret;
    std::string _comments;
};

struct listen
{
    std::string address = "0.0.0.0";
    std::string domain = "localhost";
    std::string cert;
    std::string key;
    int port = 80;
    std::string _comments;
};

struct profile
{
    std::string name;
    bool default_ = false;
    std::vector<struct listen> listen;
    std::string _comments;
};

struct application
{
    std::string name;
    std::string version = "1.0.0";
    std::string email = "example@terranova.com";
    std::string license = "MIT";
    struct auth auth;
    std::vector<struct entity> entity;
    std::vector<struct profile> profile;
    std::string namespace_ = "/";
    std::string _comments;
    bool serve_docs = true;
};
