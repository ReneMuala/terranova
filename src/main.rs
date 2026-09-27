use tracing::info;

use crate::init::spec::Spec;

mod init;
mod runtime;
#[global_allocator]
static GLOBAL: mimalloc::MiMalloc = mimalloc::MiMalloc;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    tracing_subscriber::fmt::init();
    let spec = Spec::new(None);
    info!("{spec:?}");
    Ok(())
}
