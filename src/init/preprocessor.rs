use super::types::ApplicationChildren;
use crate::init::{
    preprocessor::{
        Error::{InvalidDbName, InvalidListenIpv4Address, InvalidName, InvalidType},
        LocationPiece::{Borrowed, Owned},
    },
    types::{
        Application, BelongsTo, Entity, EntityChildren, Field, HasMany, HasOne, Listen,
        MethodChildren, Pk, Profile, Queries, QueriesChildren, Schema, SchemaChild,
    },
};
use regex::Regex;
use thiserror::Error;
#[derive(Debug, Error)]
pub enum Error {
    #[error("InteralRegexError")]
    InteralRegexError(#[from] regex::Error),
    #[error("InvalidDbName {1} at {0:?}")]
    InvalidDbName(LocationString, String),
    #[error("InvalidName {1} at {0:?}")]
    InvalidName(LocationString, String),
    #[error("InvalidListenIpv4Address {1} at {0:?}")]
    InvalidListenIpv4Address(LocationString, String),
    #[error("InvalidType {1} at {0:?}")]
    InvalidType(LocationString, String),
    #[error("InvalidHashFunction {1} at {0:?}")]
    InvalidHashFunction(LocationString, String),
    #[error("InvalidRoute {1} at {0:?}")]
    InvalidRoute(LocationString, String),
}
#[derive(Debug, Clone)]
pub enum LocationPiece<'a> {
    Owned(String),
    Borrowed(&'a str),
}

impl<'a> LocationPiece<'a> {
    pub fn str(&self) -> &str {
        match self {
            LocationPiece::Owned(str) => &str,
            LocationPiece::Borrowed(str) => str,
        }
    }
}

#[derive(Debug, Clone)]
pub struct Location<'a>(Vec<LocationPiece<'a>>);

#[derive(Debug)]
pub struct LocationString(String);

impl From<Location<'_>> for LocationString {
    fn from(value: Location<'_>) -> Self {
        let mut result = String::new();
        for i in value.0 {
            let str = i.str();
            result.push_str(&format!("{str}/"));
        }
        result.pop();

        LocationString(result)
    }
}

macro_rules! with_location {
    ($current:expr, $sc:tt, $loc:tt) => {{
        let cpy = $current;
        $loc.0.push(cpy);
        let result = { $sc };
        $loc.0.pop();
        result
    }};
}

macro_rules! regex_validate_option_or {
    ($regex:tt, $field:expr,  $error:tt, $loc:tt) => {{
        let regex = Regex::new($regex)?;
        if let Some(it) = $field {
            if !regex.is_match(it) {
                return Err($error($loc.clone().into(), it.clone()));
            }
        }
    }};
}

macro_rules! regex_validate_or {
    ($regex:tt, $field:expr,  $error:expr, $loc:tt) => {{
        let regex = Regex::new($regex)?;
        if !regex.is_match($field) {
            return Err($error($loc.clone().into(), $field.clone()));
        }
    }};
}

macro_rules! trim_opt {
    ($it: expr) => {
        $it.iter_mut().for_each(|it| *it = it.trim().to_owned())
    };
}

macro_rules! triml_opt {
    ($it: expr) => {
        $it.iter_mut()
            .for_each(|it| *it = it.trim().to_lowercase().to_owned())
    };
}

macro_rules! triml {
    ($it: expr) => {
        $it = $it.trim().to_lowercase().to_owned();
    };
}

macro_rules! trim {
    ($it: expr) => {
        $it = $it.trim().to_owned();
    };
}

macro_rules! process_name {
    ($name:expr, $loc:tt) => {
        regex_validate_or!(r#"^(\w+)$"#, &$name, InvalidName, $loc);
        triml!($name);
    };
}

macro_rules! process_route {
    ($name:expr, $loc:tt) => {
        regex_validate_or!(
            r#"^([\s\w-]+(/[\s\w-]+)*/?)$"#,
            &$name,
            Error::InvalidRoute,
            $loc
        );
        $name = $name.replace(" ", "-");
    };
}

macro_rules! process_opt_name {
    ($name:expr, $loc:tt) => {
        if let Some(name) = &mut $name {
            regex_validate_or!(r#"^(\w+)$"#, name, InvalidName, $loc);
            triml!(*name);
        }
    };
}

macro_rules! process_hash {
    ($hash:expr, $loc:tt) => {
        if let Some(mut hash_data) = $hash {
            regex_validate_or!(r#"^(sha2)$"#, &hash_data, Error::InvalidHashFunction, $loc);
            triml!(hash_data);
        }
    };
}

macro_rules! process_type {
    ($type:expr, $loc:tt) => {
        trim!($type);
        regex_validate_or!(
            r#"^(int|float|string|datetime|date|bool|file)$"#,
            &$type,
            InvalidType,
            $loc
        );
    };
}

pub fn process_profile_listen(
    location: &mut Location,
    listen: &mut Listen,
    index: usize,
) -> Result<(), Error> {
    with_location!(
        Owned(format!("listen${index}")),
        {
            regex_validate_or!(
                r#"^((?:(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)\.){3}(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)|localhost)$"#,
                &listen.address,
                InvalidListenIpv4Address,
                location
            );
            trim_opt!(listen.cert);
            trim_opt!(listen.domain);
            trim_opt!(listen.key);
            Ok(())
        },
        location
    )
}

pub fn process_profile(
    location: &mut Location,
    profile: &mut Profile,
    index: usize,
) -> Result<(), Error> {
    with_location!(
        Owned(format!("profile${index}")),
        {
            regex_validate_option_or!(
                r#"^(\w+\.sqlite)$"#,
                &profile.database,
                InvalidDbName,
                location
            );
            regex_validate_or!(r#"^(\w+)$"#, &profile.name, InvalidName, location);
            for (index, listen) in profile.listen.iter_mut().enumerate() {
                process_profile_listen(location, listen, index)?;
            }
            Ok(())
        },
        location
    )
}

pub fn process_pk(location: &mut Location, pk: &mut Pk, index: usize) -> Result<(), Error> {
    with_location!(
        Owned(format!("pk${index}")),
        {
            process_name!(pk.name, location);
            process_type!(pk.r#type, location);
            Ok(())
        },
        location
    )
}

pub fn process_field(
    location: &mut Location,
    field: &mut Field,
    index: usize,
) -> Result<(), Error> {
    with_location!(
        Owned(format!("field${index}")),
        {
            process_name!(field.name, location);
            process_type!(field.r#type, location);
            if let Some(hash) = &mut field.hash {
                regex_validate_or!(r#"^(sha2)$"#, hash, Error::InvalidHashFunction, location);
                triml!(*hash);
            };
            Ok(())
        },
        location
    )
}

pub fn process_has_many(
    location: &mut Location,
    has_many: &mut HasMany,
    index: usize,
) -> Result<(), Error> {
    with_location!(
        Owned(format!("has_many${index}")),
        {
            process_name!(has_many.name, location);
            process_name!(has_many.r#as, location);
            Ok(())
        },
        location
    )
}

pub fn process_has_one(
    location: &mut Location,
    has_one: &mut HasOne,
    index: usize,
) -> Result<(), Error> {
    with_location!(
        Owned(format!("has_one${index}")),
        {
            process_name!(has_one.name, location);
            process_name!(has_one.r#as, location);
            Ok(())
        },
        location
    )
}

pub fn process_belongs_to(
    location: &mut Location,
    belongs_to: &mut BelongsTo,
    index: usize,
) -> Result<(), Error> {
    with_location!(
        Owned(format!("belongs_to${index}")),
        {
            process_name!(belongs_to.entity, location);
            process_opt_name!(belongs_to.on, location);
            process_opt_name!(belongs_to.r#as, location);
            Ok(())
        },
        location
    )
}

pub fn process_schema(
    location: &mut Location,
    schema: &mut Schema,
    index: usize,
) -> Result<(), Error> {
    with_location!(
        Owned(format!("schema${index}")),
        {
            for (index, child) in schema.children.iter_mut().enumerate() {
                match child {
                    SchemaChild::Pk(pk) => process_pk(location, pk, index),
                    SchemaChild::Field(field) => process_field(location, field, index),
                    SchemaChild::HasMany(has_many) => process_has_many(location, has_many, index),
                    SchemaChild::HasOne(has_one) => process_has_one(location, has_one, index),
                    SchemaChild::BelongsTo(belongs_to) => {
                        process_belongs_to(location, belongs_to, index)
                    }
                }?
            }
            Ok(())
        },
        location
    )
}

pub fn process_method_children(
    location: &mut Location,
    method_children: &mut Vec<MethodChildren>,
) -> Result<(), Error> {
    for (index, child) in method_children.iter_mut().enumerate() {
        with_location!(
            Owned(format!("param${index}")),
            {
                match child {
                    MethodChildren::Param(param) => {
                        process_name!(param.name, location);
                        process_type!(param.r#type, location);
                    }
                }
            },
            location
        );
    }
    Ok(())
}

pub fn process_queries_children(
    location: &mut Location,
    queries_children: &mut QueriesChildren,
    index: usize,
) -> Result<(), Error> {
    match queries_children {
        QueriesChildren::Get(get) => with_location!(
            Owned(format!("get${index}")),
            {
                process_route!(get.name, location);
                process_method_children(location, &mut get.childreen)?;
            },
            location
        ),
        QueriesChildren::Post(post) => with_location!(
            Owned(format!("post${index}")),
            {
                process_route!(post.name, location);
                process_method_children(location, &mut post.childreen)?;
            },
            location
        ),
        QueriesChildren::Put(put) => with_location!(
            Owned(format!("put${index}")),
            {
                process_route!(put.name, location);
                process_method_children(location, &mut put.childreen)?;
            },
            location
        ),
        QueriesChildren::Delete(delete) => with_location!(
            Owned(format!("delete${index}")),
            {
                process_route!(delete.name, location);
                process_method_children(location, &mut delete.childreen)?;
            },
            location
        ),
    }
    Ok(())
}

pub fn process_queries(
    location: &mut Location,
    queries: &mut Queries,
    index: usize,
) -> Result<(), Error> {
    with_location!(
        Owned(format!("queries${index}")),
        {
            for (index, child) in &mut queries.children.iter_mut().enumerate() {
                process_queries_children(location, child, index)?
            }
            Ok(())
        },
        location
    )
}

pub fn process_entity(
    location: &mut Location,
    entity: &mut Entity,
    index: usize,
) -> Result<(), Error> {
    with_location!(
        Owned(format!("entity${index}")),
        {
            process_name!(entity.name, location);
            trim_opt!(entity.access);
            for (index, child) in entity.children.iter_mut().enumerate() {
                match child {
                    EntityChildren::Schema(schema) => process_schema(location, schema, index),
                    EntityChildren::Queries(queries) => process_queries(location, queries, index),
                }?
            }
            Ok(())
        },
        location
    )
}

pub fn process(mut app: Application) -> Result<Application, Error> {
    let mut location = Location(vec![]);
    with_location!(
        Borrowed("app"),
        {
            trim_opt!(app.author);
            trim_opt!(app.email);
            trim_opt!(app.license);
            trim_opt!(app.namespace);
            trim_opt!(app.version);
            process_name!(app.name, location);
            for (index, child) in app.children.iter_mut().enumerate() {
                match child {
                    ApplicationChildren::Profile(profile) => {
                        process_profile(&mut location, profile, index)
                    }
                    ApplicationChildren::Entity(entity) => {
                        process_entity(&mut location, entity, index)
                    }
                }?
            }
            Ok(app)
        },
        location
    )
}
