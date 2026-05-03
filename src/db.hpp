#pragma once
#include <SQLiteCpp/SQLiteCpp.h>
#include "types.hpp"

namespace db {
    SQLite::Database get_database(const struct application &app);
    std::vector<prepared_statement_metadata> init_statements(const std::vector<application> &apps);
}