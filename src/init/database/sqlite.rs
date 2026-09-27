use crate::init::{
    database::{
        Error::{NoSchema, TypeNotFound},
        SqlGen, SqlGenService,
        query_provider::{self, FieldQuery, QueryProvider},
    },
    types::{Application, ApplicationChildren, BelongsTo, Entity, Field, Pk, Schema, SchemaChild},
};
use thiserror::Error;

pub struct Sqlite;

#[derive(Debug, Error)]
pub enum Error {
    #[error("TypeNotFound: {0}")]
    TypeNotFound(String),
    #[error("QueryProviderError: {0}")]
    QueryProviderError(#[from] query_provider::Error),
    #[error("NoSchema")]
    NoSchema,
}

impl Sqlite {
    fn get_type(the_type: &str) -> Result<String, Error> {
        let stype = match the_type {
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
            Err(TypeNotFound(the_type.to_string()))
        } else {
            Ok(stype)
        }
    }

    fn get_field_declarations(
        &self,
        Schema { children }: &Schema,
        query: &QueryProvider,
    ) -> Result<String, Error> {
        let mut result = String::new();
        for child in children {
            let instruction = match child {
                SchemaChild::Pk(Pk { name, the_type }) => format!("{name} {the_type} PRIMARY KEY,"),
                SchemaChild::Field(Field {
                    name,
                    the_type,
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
                    let the_type = Sqlite::get_type(the_type)?;
                    format!("{name} {the_type} {optional} {unique},")
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
                    let the_type = {
                        let entity_pk = query.field(FieldQuery {
                            entity: &entity,
                            field: &on,
                        })?;
                        Sqlite::get_type(&entity_pk.the_type)?
                    };
                    let optional = if optional.unwrap_or(true) {
                        "NULL"
                    } else {
                        "NOT NULL"
                    };
                    format!(
                        "{name} {the_type} {optional}, FOREIGN KEY {name} REFERENCES {entity}({on}),"
                    )
                }
                _ => String::new(),
            };
            result.push_str(&instruction);
        }
        result.pop();
        Ok(result)
    }

    fn init_entity_statements(
        &self,
        entity: &Entity,
        ctx: &QueryProvider,
    ) -> Result<String, Error> {
        let schema = <Sqlite as SqlGen>::get_schema(&entity).ok_or(NoSchema)?;
        let declarations = self.get_field_declarations(schema, &ctx)?;
        Ok(format!(
            "CREATE TABLE IF NOT EXISTS {} {{ {declarations} }};",
            entity.name
        ))
    }

    fn get_insert_fields(&self, Schema { children }: &Schema) -> Result<(i16, String), Error> {
        let mut result = String::new();
        let mut field_count: i16 = 0;
        for child in children {
            let instruction = match child {
                SchemaChild::Field(Field { name, .. }) => {
                    field_count += 1;
                    format!("{name},")
                }
                SchemaChild::BelongsTo(belongs_to) => {
                    field_count += 1;
                    let name = belongs_to.name();
                    format!("{name},")
                }
                _ => String::new(),
            };
            result.push_str(&instruction);
        }
        result.pop();
        Ok((field_count, result))
    }

    fn placeholder_template(field_count: i16) -> String {
        let mut result = String::new();
        for counter in 1..=field_count {
            result.push_str(&format!("${counter},"));
        }
        result.pop();
        result
    }

    fn insert_service<'a>(&self, entity: &'a Entity) -> Result<SqlGenService<'a>, Error> {
        let schema = <Sqlite as SqlGen>::get_schema(&entity).ok_or(NoSchema)?;
        let name = &entity.name;
        let (field_count, insert_fields) = self.get_insert_fields(schema)?;
        let value_bind_args = Sqlite::placeholder_template(field_count);
        let query = format!("INSERT INTO {name} ({insert_fields}) VALUES {value_bind_args};");
        Ok(SqlGenService::Crud(query, schema))
    }
}

impl SqlGen for Sqlite {
    fn init_statements(
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

    fn services<'a>(
        &self,
        app: &'a Application,
    ) -> Result<Vec<super::SqlGenService<'a>>, Box<dyn std::error::Error + Send + Sync>> {
        let mut statements = vec![];
        for child in &app.children {
            match child {
                ApplicationChildren::Entity(entity) => match self.insert_service(entity) {
                    Ok(statement) => statements.push(statement),
                    Err(NoSchema) => continue,
                    Err(err) => return Err(err.into()),
                },
                _ => {}
            }
        }
        Ok(statements)
    }
}
