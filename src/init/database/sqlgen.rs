use crate::init::{
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
pub enum SqlGenService<'app> {
    Crud {
        r#type: ServiceType,
        name: String,
        query: String,
        params: Vec<Param>,
    },
    Query {
        r#type: ServiceType,
        name: String,
        query: String,
        params: Vec<&'app Param>,
    },
}

pub trait SqlGen {
    fn get_init_statements(
        &self,
        app: &Application,
    ) -> Result<Vec<String>, Box<dyn std::error::Error + Send + Sync>>;

    fn get_services<'a>(
        &self,
        app: &'a Application,
    ) -> Result<Vec<SqlGenService<'a>>, Box<dyn std::error::Error + Send + Sync>>;
}
