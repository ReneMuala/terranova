#pragma once
#include <vector>
#include "types.hpp"

namespace authentication {
    void init_auth(const application& apps);
    void init_auth(const std::vector<application>& apps);
}