use crate::init::{
    business::Method,
    database::ServiceType::{Create, Delete, Read, Update},
    types::{Application, Entity, EntityChildren, Param, QueriesChildren, Schema},
};

#[derive(Debug)]
pub enum ServiceType {
    Create,
    Read,
    Update,
    Delete,
}

impl ServiceType {
    pub fn from_querieschildren(childreen: &QueriesChildren) -> ServiceType {
        match childreen {
            QueriesChildren::Get(..) => Read,
            QueriesChildren::Post(..) => Create,
            QueriesChildren::Put(..) => Update,
            QueriesChildren::Delete(..) => Delete,
        }
    }
}

#[derive(Debug)]
pub enum SqlGenService {
    Login {
        query: String,
        params: Vec<Param>,
    },
    Crud {
        r#type: ServiceType,
        name: String,
        query: String,
        entity: String,
        params: Vec<Param>,
    },
    Query {
        r#type: ServiceType,
        name: String,
        query: String,
        entity: String,
        params: Vec<Param>,
    },
}

impl SqlGenService {
    pub fn values(&self) -> (&String, &Vec<Param>) {
        match self {
            SqlGenService::Login { query, params, .. }
            | SqlGenService::Crud { query, params, .. }
            | SqlGenService::Query { query, params, .. } => (query, params),
        }
    }

    pub fn entity(&self) -> Option<&String> {
        match self {
            SqlGenService::Login { .. } => None,
            SqlGenService::Crud { entity, .. } | SqlGenService::Query { entity, .. } => {
                Some(entity)
            }
        }
    }

    pub fn method(&self) -> Method {
        match self {
            SqlGenService::Login { .. } => Method::Post,
            SqlGenService::Crud { r#type, .. } | SqlGenService::Query { r#type, .. } => {
                match r#type {
                    Create => Method::Post,
                    Read => Method::Get,
                    Update => Method::Put,
                    Delete => Method::Delete,
                }
            }
        }
    }
}

pub trait SqlGen {
    fn get_init_statements(
        &self,
        app: &Application,
    ) -> Result<Vec<String>, Box<dyn std::error::Error + Send + Sync>>;

    fn get_services<'a>(
        &self,
        app: &'a Application,
    ) -> Result<Vec<SqlGenService>, Box<dyn std::error::Error + Send + Sync>>;
}
