use crate::init::{
    database::{SqlGen, Sqlite},
    types::{Application, ApplicationChildren, Field, HasMany, HasOne, Profile, SchemaChild},
};
use thiserror::Error;

pub struct QueryProvider<'a> {
    pub app: &'a Application,
}

pub struct FieldQuery<'a, 'b> {
    pub entity: &'a str,
    pub field: &'b str,
}

pub struct RelSpecQuery<'a> {
    pub strong_entity: &'a str,
    pub weak_entity: &'a str,
}

pub enum RelSpec<'a> {
    HasMany(&'a HasMany),
    HasOne(&'a HasOne),
}

impl<'a> RelSpec<'a> {
    pub fn on_delete(&self) -> String {
        match self {
            RelSpec::HasMany(it) if let Some(value) = &it.on_delete => value.clone(),
            RelSpec::HasOne(it) if let Some(value) = &it.on_delete => value.clone(),
            _ => "CASCADE".to_owned(),
        }
    }

    pub fn on_update(&self) -> String {
        match self {
            RelSpec::HasMany(it) if let Some(value) = &it.on_update => value.clone(),
            RelSpec::HasOne(it) if let Some(value) = &it.on_update => value.clone(),
            _ => "CASCADE".to_owned(),
        }
    }
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
    pub fn relation_specifier(&self, query: RelSpecQuery) -> Result<RelSpec, Error> {
        for child in &self.app.children {
            match child {
                ApplicationChildren::Entity(entity) => {
                    if entity.name == query.strong_entity {
                        let schema = entity.schema().ok_or_else(|| Error::NoSchemaInEntity {
                            entity: query.strong_entity.to_string(),
                        })?;
                        let field = schema
                            .children
                            .iter()
                            .find(|c| c.name_is(&query.weak_entity))
                            .ok_or_else(|| Error::FieldNotFoundInEntity {
                                field: query.weak_entity.to_string(),
                                entity: query.strong_entity.to_string(),
                            })?;
                        return match field {
                            SchemaChild::HasMany(has_many) => Ok(RelSpec::HasMany(&has_many)),
                            SchemaChild::HasOne(has_one) => Ok(RelSpec::HasOne(&has_one)),
                            _ => Err(Error::FieldNotFoundInEntity {
                                field: query.weak_entity.to_string(),
                                entity: query.strong_entity.to_string(),
                            }),
                        };
                    }
                }
                _ => {}
            }
        }
        Err(Error::UnsupportedQueryForField {
            field: query.weak_entity.to_string(),
            entity: query.strong_entity.to_string(),
        })
    }

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
