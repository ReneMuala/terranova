use crate::init::{database::SqlGen, types::Application};
use thiserror::Error;
use tracing::warn;

#[derive(Debug)]
pub struct ServiceSpec {}

#[derive(Debug, Error)]
pub enum Error {}

impl ServiceSpec {
    pub fn new(app: &Application, sqlgen: &impl SqlGen) -> Result<ServiceSpec, Error> {
        let db_service = sqlgen.get_services(app);
        warn!("{db_service:?}");
        Ok(ServiceSpec {})
    }
}
