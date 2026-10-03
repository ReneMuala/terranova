use crate::init::{
    database::{SqlGen, Sqlite},
    types::{Application, ApplicationChildren, Field, Profile, SchemaChild},
};
use thiserror::Error;

pub struct QueryProvider<'a> {
    pub app: &'a Application,
}

pub struct FieldQuery<'a, 'b> {
    pub entity: &'a str,
    pub field: &'b str,
}

#[derive(Debug, Error)]
pub enum Error {
    #[error("no schema in entity \"{entity}\"")]
    NoSchemaInEntity { entity: String },
    #[error("field \"{field}\" not found in entity \"{entity}\"")]
    FieldNotFoundInEntity { field: String, entity: String },
    #[error("unsopported query for field \"{field}\" in entity \"{entity}\"")]
    UnsupportedQueryForField { field: String, entity: String },
}

impl<'a> QueryProvider<'a> {
    pub fn field(&self, query: FieldQuery) -> Result<Field, Error> {
        for child in &self.app.children {
            match child {
                ApplicationChildren::Entity(entity) => {
                    if entity.name == query.entity {
                        let schema = entity.schema().ok_or_else(|| Error::NoSchemaInEntity {
                            entity: query.entity.to_string(),
                        })?;
                        let field = schema
                            .children
                            .iter()
                            .find(|c| c.name_is(&query.field))
                            .ok_or_else(|| Error::FieldNotFoundInEntity {
                                field: query.field.to_string(),
                                entity: query.entity.to_string(),
                            })?;
                        return match field {
                            SchemaChild::Pk(pk) => Ok(Field {
                                name: pk.name.clone(),
                                r#type: pk.r#type.to_string(),
                                hash: None,
                                optional: None,
                                unique: Some(true),
                            }),
                            SchemaChild::Field(field) => Ok(field.clone()),
                            _ => Err(Error::FieldNotFoundInEntity {
                                field: query.field.to_string(),
                                entity: query.entity.to_string(),
                            }),
                        };
                    }
                }
                _ => {}
            }
        }
        Err(Error::UnsupportedQueryForField {
            field: query.field.to_string(),
            entity: query.entity.to_string(),
        })
    }
}
