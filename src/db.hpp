#pragma once
#include <SQLiteCpp/SQLiteCpp.h>
#include "types.hpp"

namespace db {
    SQLite::Database& get_database(const struct application &app);
    std::tuple<std::vector<prepared_statement_metadata>,bool>  init_statements(const std::vector<application> &apps);
}