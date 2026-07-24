mod loader;
mod types;


use tracing::info;

use crate::loader::*;

fn main() {
    tracing_subscriber::fmt::init();
    match load_or_panic("app.kdl") {
        Some(apps) => {
            for app in apps {
                info!("name: {}", app.name)
            }
        },
        None => todo!(),
    }
}
