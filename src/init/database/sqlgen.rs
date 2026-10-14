use crate::init::{
    business::{DecoderType, Method},
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
    pub fn query_and_params(&self) -> (&String, &Vec<Param>) {
        match self {
            SqlGenService::Login { query, params, .. }
            | SqlGenService::Crud { query, params, .. }
            | SqlGenService::Query { query, params, .. } => (query, params),
        }
    }

    pub fn route(&self) -> String {
        match self {
            SqlGenService::Login { .. } => format!("/login"),
            SqlGenService::Crud { entity, .. } => format!("/{entity}"),
            SqlGenService::Query { name, entity, .. } => format!("/{entity}/{name}"),
        }
    }

    pub fn decoder_type(&self) -> DecoderType {
        match self {
            SqlGenService::Login { .. } => DecoderType::Body,
            SqlGenService::Crud { r#type, .. } | SqlGenService::Query { r#type, .. } => {
                match r#type {
                    Create | Update => DecoderType::Body,
                    Read | Delete => DecoderType::Query,
                }
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
