use thiserror::Error;

use crate::init::{
    database::Sqlite,
    loader,
    metadata::{self, Metadata},
    migration::{self, MigrationSpec},
    profile::{self, ProfileSpec},
    service::{self, ServiceSpec},
    spec::Error::MissingApplicationDefinition,
};

#[derive(Debug)]
pub struct Spec {
    pub metadata: Metadata,
    pub migration: MigrationSpec,
    pub profile: ProfileSpec,
    pub service: ServiceSpec,
}

#[derive(Debug, Error)]
pub enum Error {
    #[error("Syntax error: {0}")]
    SyntaxError(#[from] loader::Error),
    #[error("No application definition found")]
    MissingApplicationDefinition,
    #[error("Profile spec error: {0}")]
    ProfileError(#[from] profile::Error),
    #[error("Migration spec error: {0}")]
    MigrationError(#[from] migration::Error),
    #[error("Service spec error: {0}")]
    ServiceError(#[from] service::Error),
}

impl Spec {
    pub fn new(selected_profile: Option<String>) -> Result<Spec, Error> {
        let definition = loader::load("app.kdl")?;
        let app = definition.first().ok_or(MissingApplicationDefinition)?;
        let profile = profile::ProfileSpec::new(app, selected_profile)?;
        let metadata = Metadata::new(app);
        let sqlgen = Sqlite;
        let migration = MigrationSpec::new(app, &sqlgen)?;
        let service = ServiceSpec::new(app, &sqlgen)?;
        Ok(Spec {
            profile,
            metadata,
            migration,
            service,
        })
    }
}
