#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "types.hpp"
#include "yyjson.h"

namespace authentication {
    struct session {
        yyjson_doc * data = nullptr;
        std::unordered_set<std::string> roles = {{"protected", true}}; // all logged users have protected access

        ~session(){
            if(data){
                yyjson_doc_free(data);
            }
        }

    };
    bool init_auth(const application &app, std::unordered_map<std::string, std::optional<std::reference_wrapper<const entity>>> entity_ref_map, std::vector<prepared_statement_metadata> & stats);
}