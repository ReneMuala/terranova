//
// Created by dte on 3/4/2026.
//
#include <fstream>
#include <iostream>
#include <list>
#include <pugixml.hpp>
#include <regex>
#include <set>
#include <unordered_set>
#include <fmt/core.h>
#include <vector>
#include <glog/logging.h>
#include "CppConsoleTable.hpp"

namespace blueprint
{

    #define assert_valid(E) if(not (E)) { throw std::runtime_error(fmt::format("validation rule failed: {} in {}", #E, __FUNCTION__));}
    /*
    namespace error
    {
        // Source - https://stackoverflow.com/a/21014028
        // Posted by zeuxcg
        // Retrieved 2026-03-03, License - CC BY-SA 3.0

        typedef std::vector<ptrdiff_t> offset_data_t;

        bool build_offset_data(offset_data_t& result, const char* file)
        {
            FILE* f = fopen(file, "rb");
            if (!f) return false;

            ptrdiff_t offset = 0;

            char buffer[1024];
            size_t size;

            while ((size = fread(buffer, 1, sizeof(buffer), f)) > 0)
            {
                for (size_t i = 0; i < size; ++i)
                    if (buffer[i] == '\n')
                        result.push_back(offset + i);

                offset += size;
            }

            fclose(f);

            return true;
        }

        std::pair<int, int> get_location(const offset_data_t& data, ptrdiff_t offset)
        {
            offset_data_t::const_iterator it = std::lower_bound(data.begin(), data.end(), offset);
            size_t index = it - data.begin();

            return std::make_pair(1 + index, index == 0 ? offset + 1 : offset - data[index - 1]);
        }
    }
    */
    inline bool no_other_attribute_than(const pugi::xml_node & node, const std::unordered_set<std::string> & attributes)
    {
        for (const auto & attribute : node.attributes())
        {
            if (not attributes.contains(attribute.name()))
                return false;
        }
        return true;
    }
    inline bool no_attribute(const pugi::xml_node & node)
    {
        return node.attributes().empty();
    }

    struct field
    {
        std::string name;
        std::string type;
        static std::shared_ptr<field> make(const pugi::xml_node & node)
        {
            assert_valid(no_other_attribute_than(node, {"name", "type"}));
            auto new_field = std::make_shared<field>(node.attribute("name").value(), node.attribute("type").value());
            assert_valid(not new_field->name.empty());
            assert_valid(not new_field->type.empty());
            return new_field;
        }
    };

    struct relation
    {
        std::string target;
        std::string alias;
        std::string on_delete;

        std::string alias_or_target_name()
        {
            return alias.empty() ? target : alias;
        }
    };

    static bool register_name_if_unique(const std::string & name, std::set<std::string> & declared_names)
    {
        if (not declared_names.contains(name))
        {
            declared_names.insert(name);
            return true;
        }
        return false;
    }

    struct schema
    {
        std::vector<std::shared_ptr<field>> fields;
        std::shared_ptr<field> pk;
        std::vector<relation> has_many, has_one, belongs_to;
        std::set<std::string> names;

        static void print(const std::shared_ptr<schema> & schema,std::ostream & out)
        {
            if (schema)
            {
                samilton::ConsoleTable table(1, 1, samilton::Alignment::left);
                for (int i = 0; i < schema->fields.size(); ++i)
                {
                    table[i][0] = schema->fields[i]->name;
                    table[i][1] = schema->fields[i]->type;
                }
                out << table;
            }
        }

        static std::shared_ptr<schema> make(const pugi::xml_node & node)
        {
            assert_valid(no_attribute(node));
            auto new_entoty_schema = std::make_shared<schema>();
            for (auto & child : node.children())
            {
                if (child.name() == std::string("field"))
                {
                    auto new_field = field::make(child);
                    assert_valid(register_name_if_unique(new_field->name, new_entoty_schema->names));
                    new_entoty_schema->fields.push_back(new_field);
                } else if (child.name() == std::string("pk"))
                {
                    assert_valid(not new_entoty_schema->pk);
                    new_entoty_schema->pk = field::make(child);
                    assert_valid(register_name_if_unique(new_entoty_schema->pk->name, new_entoty_schema->names));
                } else if (child.name() == std::string("has-many"))
                {
                    auto new_relation = child;
                    assert_valid(no_other_attribute_than(new_relation, {"entity", "as", "on-delete"}));
                    auto has_many = relation {
                        .target =child.attribute("entity").value(),
                        .alias =child.attribute("as").value(),
                        .on_delete =child.attribute("on-delete").value(),
                        };
                    assert_valid(has_many.on_delete == std::string("cascade") or has_many.on_delete == std::string("set null") or has_many.on_delete == std::string("set default") or has_many.on_delete == std::string(""));
                    assert_valid(register_name_if_unique(has_many.alias_or_target_name(), new_entoty_schema->names));
                    new_entoty_schema->has_many.emplace_back(has_many);
                } else if (child.name() == std::string("has-one"))
                {
                    auto new_relation = child;
                    assert_valid(no_other_attribute_than(new_relation, {"entity", "as", "on-delete"}));
                    auto has_one = relation {
                        .target =child.attribute("entity").value(),
                        .alias =child.attribute("as").value(),
                        .on_delete =child.attribute("on-delete").value(),
                        };
                    assert_valid(has_one.on_delete == std::string("cascade") or has_one.on_delete == std::string("set null") or has_one.on_delete == std::string("set default") or has_one.on_delete == std::string(""));
                    assert_valid(register_name_if_unique(has_one.alias_or_target_name(), new_entoty_schema->names));
                    auto o = node.offset_debug();
                    new_entoty_schema->has_one.emplace_back(has_one);
                } else if (child.name() == std::string("belongs-to"))
                {
                    auto new_relation = child;
                    assert_valid(no_other_attribute_than(new_relation, {"entity", "as", "on-delete"}));
                    auto belongs_to = relation {
                        .target =child.attribute("entity").value(),
                        .alias =child.attribute("as").value(),
                        .on_delete =child.attribute("on-delete").value(),
                        };
                    assert_valid(belongs_to.on_delete == std::string(""));
                    assert_valid(register_name_if_unique(belongs_to.alias_or_target_name(), new_entoty_schema->names));
                    auto o = node.offset_debug();
                    new_entoty_schema->belongs_to.emplace_back(belongs_to);
                } else
                {
                    throw std::runtime_error(fmt::format("unknown parameter <{}>...", child.name()));
                }
            }
            new_entoty_schema->names.clear(); // not needed any more
            return new_entoty_schema;
        }
    };

    struct hook
    {
        enum hook_type
        {
            before,
            after,
            unknown
        };
        std::string event;
        std::string script;
        std::string function;
        hook_type type = unknown;
        static std::shared_ptr<hook> make(const pugi::xml_node & node)
        {
            assert_valid(no_other_attribute_than(node, {"event", "script", "fn"}));
            auto new_hook = std::make_shared<hook>(node.attribute("event").value(), node.attribute("script").value(), node.attribute("fn").value());
            if (node.name() == std::string("before"))
            {
                new_hook->type = before;
            } else if (node.name() == std::string("after"))
            {
                new_hook->type = after;
            }
            assert_valid(new_hook->type == before or new_hook->type == after);
            assert_valid(not new_hook->event.empty());
            assert_valid(not new_hook->script.empty());
            assert_valid(not new_hook->function.empty());
            return new_hook;
        }
    };

    struct hooks
    {
        std::vector<std::shared_ptr<hook>> hooks_;
        static std::shared_ptr<hooks> make(const pugi::xml_node & node)
        {
            assert_valid(no_attribute(node));
            auto new_hooks = std::make_shared<hooks>();
            for (auto & child : node.children())
            {
                if (child.name() == std::string("before") or child.name() == std::string("after"))
                {
                    new_hooks->hooks_.push_back(hook::make(child));
                } else
                {
                    throw std::runtime_error(fmt::format("unknown hook <{}>...", child.name()));
                }
            }
            return new_hooks;
        }
    };

    struct query
    {
        enum query_method
        {
            unspecified,
            POST,
            GET,
            PUT,
            PATCH,
            DELETE,
        };
        std::string name;
        std::string sql;
        query_method method = unspecified;
        std::vector<std::shared_ptr<field>> params;
        std::set<std::string> names;

        static std::shared_ptr<query> make(const pugi::xml_node & node, const std::string & table)
        {
            assert_valid(no_other_attribute_than(node, {"name","method"}));
            auto new_query = std::make_shared<query>(node.attribute("name").value());
            assert_valid(no_attribute(node.child("sql")));
            assert_valid(no_attribute(node.child("params")));
            new_query->sql = node.child("sql").text().as_string();
            new_query->sql = std::regex_replace(new_query->sql,std::regex("\\{table\\}"), table);
            const auto & method = std::string(node.attribute("method").value());
            if (method == "post")
                new_query->method = POST;
            else if (method == "get")
                new_query->method = GET;
            else if (method == "put")
                new_query->method = PUT;
            else if (method == "patch")
                new_query->method = PATCH;
            else if (method == "delete")
                new_query->method = DELETE;
            for (auto & child : node.child("params").children())
            {
                if (child.name() == std::string("param"))
                {
                    auto new_query_param = field::make(child);
                    assert_valid(register_name_if_unique(new_query_param->name, new_query->names));
                    new_query->params.push_back(new_query_param);
                } else
                {
                    throw std::runtime_error(fmt::format("unknown param type <{}>...", child.name()));
                }
            }
            assert_valid(new_query->method != unspecified);
            assert_valid(not new_query->name.empty());
            assert_valid(not new_query->sql.empty());
            return new_query;
        }
    };

    struct queries
    {
        std::vector<std::shared_ptr<query>> queries_;
        std::set<std::string> names;

        static std::shared_ptr<queries> make(const pugi::xml_node & node, const std::string & table)
        {
            assert_valid(no_attribute(node));
            auto new_queries = std::make_shared<queries>();
            for (auto & child : node.children())
            {
                if (child.name() == std::string("query"))
                {
                    auto new_query = query::make(child, table);
                    assert_valid(register_name_if_unique(new_query->name, new_queries->names));
                    new_queries->queries_.push_back(new_query);
                } else
                {
                    throw std::runtime_error(fmt::format("unknown queries param <{}>...", child.name()));
                }
            }
            return new_queries;
        }
    };
    struct entity
    {
        std::string name;
        std::string access = "private";
        std::shared_ptr<schema> schema;
        std::shared_ptr<hooks> hooks;
        std::shared_ptr<queries> queries;
        static void print(const std::shared_ptr<entity> & entity,std::ostream & out)
        {
            if (entity)
            {
                out << entity->name << " (Entity)\n";
                schema::print(entity->schema, out);
            }
        }
        static std::shared_ptr<entity> make(const pugi::xml_node & node)
        {
            assert_valid(no_other_attribute_than(node, {"name", "access"}));
            assert_valid(node.name() == std::string("entity"));
            auto new_entity = std::make_shared<entity>(node.attribute("name").value());
            if (not node.attribute("access").empty())
            {
                new_entity->access = node.attribute("access").value();
            }
            assert_valid(not new_entity->name.empty());
            assert_valid(new_entity->access == "public" or new_entity->access == "private" or new_entity->access == "protected");
            for (auto & child : node.children())
            {
                if (child.name() == std::string("schema"))
                {
                    const auto & entity_schema = child;
                    assert_valid(no_attribute(entity_schema));
                    assert_valid(not new_entity->schema);
                    new_entity->schema = schema::make(entity_schema);
                } else if (child.name() == std::string("hooks"))
                {
                    const auto & entity_hooks = child;
                    assert_valid(no_attribute(entity_hooks));
                    assert_valid(not new_entity->hooks);
                    new_entity->hooks = hooks::make(entity_hooks);
                } else if (child.name() == std::string("queries"))
                {
                    const auto & entity_queries = child;
                    assert_valid(no_attribute(entity_queries));
                    assert_valid(not new_entity->queries);
                    new_entity->queries = queries::make(entity_queries, new_entity->name);
                } else
                {
                    throw std::runtime_error(fmt::format("unknown parameter <{}>...", child.name()));
                }
            }
            return new_entity;
        }
    };

    struct blueprint
    {
        std::string name;
        std::vector<std::shared_ptr<entity>> entities;
        std::set<std::string> names;

        static void print(const std::shared_ptr<blueprint> & blueprint,std::ostream & out)
        {
            if (blueprint)
            {
                for (auto & child : blueprint->entities)
                {
                    entity::print(child, out);
                }
            }
        }
        static std::shared_ptr<blueprint> make(const pugi::xml_node & node)
        {
            auto new_blueprint = std::make_shared<blueprint>(node.attribute("name").value());
            assert_valid(not new_blueprint->name.empty());
            for (auto & child : node.children())
            {
                if (child.name() == std::string("entity"))
                {
                    auto new_entity = entity::make(child);
                    assert_valid(register_name_if_unique(new_entity->name, new_blueprint->names));
                    new_blueprint->entities.push_back(new_entity);
                }
            }
            assert_valid(not new_blueprint->entities.empty());
            return new_blueprint;
        }
    };
}
