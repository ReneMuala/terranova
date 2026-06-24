#include "authentication.hpp"
#include "db.hpp"
#include "fmt/format.h"
#include "misc.hpp"
#include "types.hpp"
#include <exception>
#include <fmt/core.h>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <glog/logging.h>

namespace db {
prepared_statement_metadata init_stmt_login(const SQLite::Database &database, const entity &entity,
                                            unsigned long long index, const auth &auth);
prepared_statement_metadata init_stmt_logout(const SQLite::Database &database, const entity &entity,
                                             unsigned long long index, const auth &auth);
prepared_statement_metadata init_stmt_role(const SQLite::Database &database, const entity &entity,
                                           unsigned long long index, const struct auth &auth, const struct role &role);
} // namespace db

#define THROW_WHEN_EMPTY(X)                                                                        \
    if (X.empty())                                                                                 \
    throw std::runtime_error(                                                                      \
        fmt::format("incomplete auth configuration: field \"{}\" was not specified", #X))

#define THROW_WHEN_NOT_EMPTY_WITHOUT_PROVIDER(X)                                                   \
    if (not X.empty())                                                                             \
    throw std::runtime_error(fmt::format(                                                          \
        "meaningless auth configuration: field \"{}\" was specified without \"auth.provider\"",    \
        #X))

extern unsigned long long global_statement_index;

bool authentication::init_auth(
    const application &app,
    std::unordered_map<std::string, std::optional<std::reference_wrapper<const entity>>>
        entity_ref_map,
    std::vector<prepared_statement_metadata> &stats) {
    auto &auth = app.auth;

    if (not auth.provider.empty()) {
        LOG(INFO) << "generating auth queries";
        THROW_WHEN_EMPTY(auth.identity);
        THROW_WHEN_EMPTY(auth.secret);
        try {
            const entity &provider = entity_ref_map.at(misc::tolower(auth.provider))->get();
            stats.push_back(db::init_stmt_login(db::get_database(app), provider,
                                                global_statement_index++, auth));
            stats.push_back(db::init_stmt_logout(db::get_database(app), provider,
                                                 global_statement_index++, auth));
            for (const auto &r : auth.role) {
                auto & db = db::get_database(app);
                stats.push_back(db::init_stmt_role(db, provider,
                                                   global_statement_index++, auth, r));
            }
            return true;
        } catch (std::out_of_range &e) {
            throw std::runtime_error(
                fmt::format("no such auth provider \"{}\"", auth.provider, e.what()));
        }
    } else {
        THROW_WHEN_NOT_EMPTY_WITHOUT_PROVIDER(auth.identity);
        THROW_WHEN_NOT_EMPTY_WITHOUT_PROVIDER(auth.secret);
        THROW_WHEN_NOT_EMPTY_WITHOUT_PROVIDER(auth.hash);
        THROW_WHEN_NOT_EMPTY_WITHOUT_PROVIDER(auth.role);
    }
    return false;
}