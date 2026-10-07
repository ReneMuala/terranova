use crate::init::{
    database::{
        Error::{NoPk, NoSchema, TypeNotFound},
        SqlGen, SqlGenService,
        query_provider::{self, FieldQuery, QueryProvider, RelSpecQuery},
    },
    types::{
        Application, ApplicationChildren, BelongsTo, Entity, Field, Param, Pk, QueriesChildren,
        Schema, SchemaChild,
    },
};
use thiserror::Error;

use super::ServiceType;

pub struct Sqlite;

#[derive(Debug, Error)]
pub enum Error {
    #[error("TypeNotFound: {0}")]
    TypeNotFound(String),
    #[error("QueryProviderError: {0}")]
    QueryProviderError(#[from] query_provider::Error),
    #[error("NoSchema")]
    NoSchema,
    #[error("NoPk")]
    NoPk,
}

impl Sqlite {
    fn get_type(r#type: &str) -> Result<String, Error> {
        let stype = match r#type {
            "int" => "INTEGER",
            "float" => "REAL",
            "string" => "TEXT",
            "blob" => "BLOB",
            "file" => "BLOB",
            "date" => "DATE",
            "datetime" => "DATETIME",
            "bool" => "BOOL",
            _ => "",
        }
        .to_string();

        if stype.is_empty() {
            Err(TypeNotFound(r#type.to_string()))
        } else {
            Ok(stype)
        }
    }

    fn get_field_declarations(
        &self,
        entity_name: &str,
        Schema { children }: &Schema,
        qp: &QueryProvider,
    ) -> Result<String, Error> {
        let mut result = String::new();
        for child in children {
            let instruction = match child {
                SchemaChild::Pk(Pk { name, r#type: ty }) => format!("{name} {ty} PRIMARY KEY,"),
                SchemaChild::Field(Field {
                    name,
                    r#type,
                    optional,
                    unique,
                    ..
                }) => {
                    let optional = if optional.unwrap_or(true) {
                        "NULL"
                    } else {
                        "NOT NULL"
                    };
                    let unique = if unique.unwrap_or(false) {
                        "UNIQUE"
                    } else {
                        ""
                    };
                    let ty = Sqlite::get_type(r#type)?;
                    format!("{name} {ty} {optional} {unique},")
                }
                SchemaChild::BelongsTo(belongs_to) => {
                    let name = belongs_to.name();
                    let BelongsTo {
                        entity,
                        on,
                        optional,
                        ..
                    } = belongs_to;
                    let on = on.clone().unwrap_or(format!("id"));
                    let r#type = {
                        let entity_pk = qp.field(FieldQuery {
                            entity: &entity,
                            field: &on,
                        })?;
                        Sqlite::get_type(&entity_pk.r#type)?
                    };
                    let optional = if optional.unwrap_or(true) {
                        "NULL"
                    } else {
                        "NOT NULL"
                    };
                    let ty = r#type;

                    let (on_delete, on_update) = {
                        let relspec = qp.relation_specifier(RelSpecQuery {
                            strong_entity: &name,
                            weak_entity: entity_name,
                        });

                        match relspec {
                            Ok(rel) => (rel.on_delete(), rel.on_update()),
                            Err(_) => ("CASCADE".to_owned(), "CASCADE".to_owned()),
                        }
                    };

                    format!(
                        "{name} {ty} {optional}, FOREIGN KEY {name} REFERENCES {entity}({on}) ON UPDATE {on_update} ON DELETE {on_delete},"
                    )
                }
                _ => String::new(),
            };
            result.push_str(&instruction);
        }
        result.pop();
        Ok(result)
    }

    fn init_entity_statements(&self, entity: &Entity, qp: &QueryProvider) -> Result<String, Error> {
        let schema = entity.schema().ok_or(NoSchema)?;
        let declarations = self.get_field_declarations(&entity.name, schema, &qp)?;
        Ok(format!(
            "CREATE TABLE IF NOT EXISTS {} {{ {declarations} }};",
            entity.name
        ))
    }

    fn get_insert_fields(
        &self,
        Schema { children }: &Schema,
        qp: &QueryProvider,
    ) -> Result<(String, Vec<Param>), Error> {
        let mut result = String::new();
        let mut params: Vec<Param> = vec![];
        for child in children {
            let instruction = match child {
                SchemaChild::Field(field) => {
                    params.push(Param::from_field(field));
                    format!("{},", field.name)
                }
                SchemaChild::BelongsTo(belongs_to) => {
                    let name = belongs_to.name();
                    let param = {
                        let r#type = {
                            let entity_pk = qp.field(FieldQuery {
                                entity: &belongs_to.entity,
                                field: &belongs_to.on.clone().unwrap_or("id".to_owned()),
                            })?;
                            Sqlite::get_type(&entity_pk.r#type)?
                        };

                        Param {
                            name: name.clone(),
                            r#type: r#type,
                            default: None,
                            value: None,
                        }
                    };
                    params.push(param);
                    format!("{name},")
                }
                _ => String::new(),
            };
            result.push_str(&instruction);
        }
        result.pop();

        Ok((result, params))
    }

    fn placeholder_template(field_count: usize) -> String {
        let mut result = String::new();
        for counter in 1..=field_count {
            result.push_str(&format!("${counter},"));
        }
        result.pop();
        result
    }

    fn get_insert_service<'a>(
        &self,
        entity: &'a Entity,
        qp: &QueryProvider,
    ) -> Result<SqlGenService<'a>, Error> {
        let schema = entity.schema().ok_or(NoSchema)?;
        let name = &entity.name;
        let (insert_fields, params) = self.get_insert_fields(schema, qp)?;
        let value_bind_args = Sqlite::placeholder_template(params.len());
        let query = format!("INSERT INTO {name} ({insert_fields}) VALUES {value_bind_args};");
        Ok(SqlGenService::Crud {
            r#type: ServiceType::Create,
            name: name.clone(),
            query,
            params,
        })
    }

    fn get_update_fields(
        &self,
        Schema { children }: &Schema,
        qp: &QueryProvider,
    ) -> Result<(String, Vec<Param>), Error> {
        let mut result = String::new();
        let mut params: Vec<Param> = vec![];
        for child in children {
            let instruction = match child {
                SchemaChild::Field(field) => {
                    params.push(Param::from_field(field));
                    format!("{} = ${},", field.name, params.len())
                }
                SchemaChild::BelongsTo(belongs_to) => {
                    let name = belongs_to.name();
                    let param = {
                        let r#type = {
                            let entity_pk = qp.field(FieldQuery {
                                entity: &belongs_to.entity,
                                field: &belongs_to.on.clone().unwrap_or("id".to_owned()),
                            })?;
                            Sqlite::get_type(&entity_pk.r#type)?
                        };

                        Param {
                            name: name.clone(),
                            r#type: r#type,
                            default: None,
                            value: None,
                        }
                    };
                    params.push(param);
                    format!("{} = ${},", name, params.len())
                }
                _ => String::new(),
            };
            result.push_str(&instruction);
        }
        result.pop();

        Ok((result, params))
    }

    fn get_update_service<'a>(
        &self,
        entity: &'a Entity,
        qp: &QueryProvider,
    ) -> Result<SqlGenService<'a>, Error> {
        let schema = entity.schema().ok_or(NoSchema)?;
        let pk = schema.pk().ok_or(NoPk)?;
        let name = &entity.name;
        let (update_fields, mut params) = self.get_update_fields(schema, qp)?;
        params.push(Param::from_pk(pk));
        let (pk_name, param_len) = (&pk.name, params.len());
        let query = format!("UPDATE {name} SET {update_fields} WHERE {pk_name} = ${param_len};");
        Ok(SqlGenService::Crud {
            r#type: ServiceType::Update,
            name: name.clone(),
            query,
            params,
        })
    }

    fn get_select_fields(&self, Schema { children }: &Schema) -> Result<String, Error> {
        let mut result = String::new();
        for child in children {
            let instruction = match child {
                SchemaChild::Pk(Pk { name, .. }) | SchemaChild::Field(Field { name, .. }) => {
                    format!("{name},")
                }
                SchemaChild::BelongsTo(belongs_to) => {
                    let name = belongs_to.name();
                    format!("{},", name)
                }
                _ => String::new(),
            };
            result.push_str(&instruction);
        }
        result.pop();

        Ok(result)
    }

    fn get_select_service<'a>(&self, entity: &'a Entity) -> Result<SqlGenService<'a>, Error> {
        let schema = entity.schema().ok_or(NoSchema)?;
        let name = &entity.name;
        let select_fields = self.get_select_fields(schema)?;
        let query = format!("SELECT {select_fields} FROM {name} LIMIT $1 OFFSET $2;");
        Ok(SqlGenService::Crud {
            r#type: ServiceType::Read,
            query,
            name: name.clone(),
            params: vec![
                Param::from_tuple(("limit".to_owned(), "int".to_owned())),
                Param::from_tuple(("offset".to_owned(), "int".to_owned())),
            ],
        })
    }

    fn get_delete_service<'a>(&self, entity: &'a Entity) -> Result<SqlGenService<'a>, Error> {
        let schema = entity.schema().ok_or(NoSchema)?;
        let pk = schema.pk().ok_or(NoPk)?;
        let name = &entity.name;
        let params = vec![Param::from_pk(pk)];
        let (pk_name, param_len) = (&pk.name, params.len());
        let query = format!("DELETE {name} WHERE {pk_name} = ${param_len};");
        Ok(SqlGenService::Crud {
            r#type: ServiceType::Delete,
            name: name.clone(),
            query,
            params,
        })
    }

    fn get_custom_service<'a>(&self, query: &'a QueriesChildren) -> SqlGenService<'a> {
        let r#type = ServiceType::from_querieschildren(&query);
        let params = query.params();
        let (name, query) = query.values();
        SqlGenService::Query {
            r#type,
            name: name.clone(),
            query: query.clone(),
            params,
        }
    }
}

impl SqlGen for Sqlite {
    fn get_init_statements(
        &self,
        app: &Application,
    ) -> Result<Vec<String>, Box<dyn std::error::Error + Send + Sync>> {
        let mut statements = vec![];
        let ctx: QueryProvider = QueryProvider { app };

        for child in &app.children {
            match child {
                ApplicationChildren::Entity(entity) => {
                    match self.init_entity_statements(entity, &ctx) {
                        Ok(statement) => statements.push(statement),
                        Err(NoSchema) => continue,
                        Err(err) => return Err(err.into()),
                    }
                }
                _ => {}
            }
        }
        Ok(statements)
    }

    fn get_services<'a>(
        &self,
        app: &'a Application,
    ) -> Result<Vec<super::SqlGenService<'a>>, Box<dyn std::error::Error + Send + Sync>> {
        let mut statements = vec![];
        let qp: QueryProvider = QueryProvider { app };

        for child in &app.children {
            match child {
                ApplicationChildren::Entity(entity) => {
                    match self.get_select_service(entity) {
                        Ok(statement) => statements.push(statement),
                        Err(NoSchema) => {}
                        Err(err) => return Err(err.into()),
                    };
                    match self.get_insert_service(entity, &qp) {
                        Ok(statement) => statements.push(statement),
                        Err(NoSchema) => {}
                        Err(err) => return Err(err.into()),
                    };
                    match self.get_update_service(entity, &qp) {
                        Ok(statement) => statements.push(statement),
                        Err(NoPk) | Err(NoSchema) => {}
                        Err(err) => return Err(err.into()),
                    };
                    match self.get_delete_service(entity) {
                        Ok(statement) => statements.push(statement),
                        Err(NoPk) | Err(NoSchema) => {}
                        Err(err) => return Err(err.into()),
                    };

                    if let Some(queries) = entity.queries() {
                        for child in &queries.children {
                            statements.push(self.get_custom_service(child));
                        }
                    }
                }
                _ => {}
            }
        }
        Ok(statements)
    }
}
