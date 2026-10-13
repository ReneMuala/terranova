use tracing::info;

use crate::{
    init::{Program, Spec},
    runtime::Runtime,
};
use miette::IntoDiagnostic;
mod init;
mod runtime;
#[global_allocator]
static GLOBAL: mimalloc::MiMalloc = mimalloc::MiMalloc;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    tracing_subscriber::fmt::init();
    let spec = Spec::new(None)?;
    // spec (sql queries) -> program (c source code, with callable functions) -> runtime (compied handlers associated to endpoints)
    info!("{spec:?}");
    let program = Program::from_spec(spec)?;
    info!("{program:?}");
    // let rt = Runtime::new(spec);
    Ok(())
}
