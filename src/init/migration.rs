use thiserror::Error;

use crate::init::{database::SqlGen, types::Application};

#[derive(Debug)]
pub struct MigrationSpec {
    queries: Vec<String>,
}

#[derive(Debug, Error)]
pub enum Error {
    #[error("SqlGenError: {0}")]
    SqlGenError(#[from] Box<dyn std::error::Error + Send + Sync>),
}

impl MigrationSpec {
    pub fn new(app: &Application, sqlgen: &impl SqlGen) -> Result<MigrationSpec, Error> {
        let queries = sqlgen.get_init_statements(app)?;
        Ok(MigrationSpec { queries })
    }
}
