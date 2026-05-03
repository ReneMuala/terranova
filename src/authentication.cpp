#include "authentication.hpp"

void authentication::init_auth(const application &app)
{
    // app.auth

}

void authentication::init_auth(const std::vector<application> &apps)
{
    for(const auto & app : apps){
        init_auth(app);
    }
}
