#include "authentication.hpp"
#include <fmt/core.h>

#define THROW_WHEN_EMPTY(X)\
        if(X.empty()) throw std::runtime_error(fmt::format("incomplete auth configuration: field \"{}\" was not specified", #X))   

#define THROW_WHEN_NOT_EMPTY(X)\
        if(not X.empty()) throw std::runtime_error(fmt::format("meaningless auth configuration: field \"{}\" was specified without \"auth.provider\"", #X))   

void authentication::init_auth(const application &app)
{
    // app.auth
    auto & auth = app.auth;
    if(not auth.provider.empty()){
        THROW_WHEN_EMPTY(auth.identity);
        THROW_WHEN_EMPTY(auth.secret);
    } else {
        THROW_WHEN_NOT_EMPTY(auth.identity);
        THROW_WHEN_NOT_EMPTY(auth.secret);
        THROW_WHEN_NOT_EMPTY(auth.hash);
        THROW_WHEN_NOT_EMPTY(auth.role);
    }
}

void authentication::init_auth(const std::vector<application> &apps)
{
    for(const auto & app : apps){
        init_auth(app);
    }
}
