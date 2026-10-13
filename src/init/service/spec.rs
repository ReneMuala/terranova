use crate::init::{
    database::{SqlGen, SqlGenService},
    types::Application,
};
use thiserror::Error;
use tracing::warn;

#[derive(Debug)]
pub struct ServiceSpec {
    pub services: Vec<SqlGenService>,
}

#[derive(Debug, Error)]
pub enum Error {
    #[error("SqlGenError: {0}")]
    SqlGenError(#[from] Box<dyn std::error::Error + Send + Sync>),
}

impl ServiceSpec {
    pub fn new(app: &Application, sqlgen: &impl SqlGen) -> Result<ServiceSpec, Error> {
        let services = sqlgen.get_services(app)?;
        Ok(ServiceSpec { services })
    }
}
