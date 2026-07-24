mod loader;
mod types;
mod runtime;

use tracing::info;

use crate::loader::*;
use crate::runtime::{JitRuntime};


fn main() {
    tracing_subscriber::fmt::init();
    let mut rt = JitRuntime::new();

    rt.compile(r#"
        #include <stdio.h>
        void greet(){
            printf("Hello world from jit!");
        }
        "#).expect("compilation failed");
    let callable = rt.get("greet").expect("failed to get callable");
    callable.call();
    match load_or_panic("app.kdl") {
        Some(apps) => {
            for app in apps {
                info!("name: {}", app.name);
                // let_cxx_string!(code = rsstr);
                // compile(&code);
                // let_cxx_string!(fname = "bye");
                // let func = get_func(&fname);
                // call(func);
                // run(String::from());
            }
        },
        None => todo!(),
    }
}
