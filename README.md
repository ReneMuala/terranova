
# Terranova

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Documentation](https://img.shields.io/badge/docs-landia.dev%2Fterranova-green)](https://landia.dev/terranova)

Terranova is a declarative language for defining REST APIs. It is based on the [KDL document language](https://kdl.dev) and can be considered a structured subset of it. A Terranova specification file defines the structure, data access, and presentation layers of an application in a single file, which is then compiled into a fully functional REST API server.

## What is Terranova?

Terranova eliminates the need to write boilerplate server code and route handlers by allowing developers to describe an application declaratively. Data models are defined in a structured schema, SQL queries are written when needed, and presentation is specified through templates — Terranova handles the compilation and execution. A Terranova specification file defines:

- **Entities** — the data models of your application
- **Schemas** — the structure and relationships of each entity
- **Queries** — raw SQL or composed batches of multiple queries
- **Views** — routes and the templates that render them
- **Profiles** — environment-specific server configuration

From a single `.kdl` file, Terranova compiles a REST API ready to serve requests.

## Core Concepts

### Entities

Entities are the central building block of a Terranova application. Each entity represents a domain object — such as a `User` or `Todo` — and encapsulates its schema, queries, and views in one place.

```kdl
entity "User" {
    schema { ... }
    queries { ... }
    views { ... }
}
```

### Queries

Queries define how data is retrieved. Terranova supports two forms:

- **Raw queries** — direct SQL with typed parameters
```kdl
get "user_by_id" sql="SELECT user.* FROM user WHERE id = :id" {
    param "id" type="int"
}
```
- **Composed queries** — batches of multiple queries executed in a single call, returning all results together under named keys
```kdl
get "user_with_stats" {
    param "id" type="int"
    data "user" {
        bind "id"
    }
    data "stats" {
        bind "id"
    }
}
```

### Views

Views bind routes to queries and render the results as HTML. They act as an alias to a query, converting its output into an HTML response using either an inline template or an external file.

```kdl
template "/" query="user_by_id" html="
    <h1>Hello, {{data.name}}!</h1>
"
```

### Profiles

Profiles allow the same specification to run under different environment configurations — for example, a development server on port 80 and a production server with TLS on port 443.

```kdl
profile "prod" {
    listen address="0.0.0.0" port=443 cert="example.crt" key="example.key"
}
```

## A Minimal Example

```kdl
application "Todo" version="0.1" {
    entity "User" {
        schema {
            pk "id" type="int"
            field "name" type="string"
            field "email" type="string"
        }

        queries {
            get "user_by_id" sql="SELECT user.* FROM user WHERE id = :id" {
                param "id" type="int"
            }
        }

        views {
            template "/" query="user_by_id" html="
                <h1>{{data.name}}</h1>
                <p>{{data.email}}</p>
            "
        }
    }

    profile "dev" default=true {
        listen address="0.0.0.0" port=80
    }
}
```

## What Terranova is Not

Terranova is not a general-purpose programming language. It is intentionally scoped to the definition of REST APIs. In its current form, business logic beyond data retrieval and presentation is not yet supported, though it is planned for future releases.

## Documentation

Full documentation is available at [landia.dev/terranova/](https://landia.dev/terranova/).

## License

This project is licensed under the terms of the MIT license.
