mod loader;
mod types;
mod runtime;

use tokio::task::JoinSet;
use tracing::{error, info};

use crate::loader::*;
use crate::runtime::{JitRuntime};

use std::collections::HashMap;
use std::convert::Infallible;
use std::{eprintln, format, vec};
use std::net::SocketAddr;

use http_body_util::Full;
use hyper::body::Bytes;
use hyper::server::conn::http1;
use hyper::service::service_fn;
use hyper::{Request, Response, StatusCode};
use hyper_util::rt::TokioIo;
use tokio::net::TcpListener;
async fn handler(req: Request<hyper::body::Incoming>) -> Result<Response<Full<Bytes>>, Infallible> {
    let callback: fn(_: Request<hyper::body::Incoming>)->Response<Full<Bytes>> = |r: Request<hyper::body::Incoming>| {
        Response::new(Full::new(Bytes::from(format!("Hello world, you are at: {}", r.uri().path()))))
    };

    let map: HashMap<String, fn(_: Request<hyper::body::Incoming>)->Response<Full<Bytes>>> = HashMap::from([(String::from("/home"), callback)]);

    let response = if let Some(handler) = map.get(req.uri().path()) {
        handler(req)
    } else {
        Response::builder()
            .status(StatusCode::NOT_FOUND)
            .body(Full::new(Bytes::new()))
            .unwrap_or(Response::default())
    };

    Ok(response)
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
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

    let addrs = vec![SocketAddr::from(([127, 0, 0, 1], 8000)),SocketAddr::from(([0, 0, 0, 0], 8001))];
    let mut set = JoinSet::new();
    for addr in addrs {
        set.spawn(async move {
           let listener = TcpListener::bind(addr).await.expect("failed to bind address");

           loop {
               let Ok((stream, _))  = listener.accept().await else {
                   error!("failed to accept connection");
                   continue;
               };
               let io = TokioIo::new(stream);
               tokio::spawn(async move {
                   if let Err(err) = http1::Builder::new()
                       .serve_connection(io, service_fn(handler))
                       .await {
                           error!("Error serving connection: {:?}", err)
                       }
               });
           }
        });
    }
    set.join_all().await;
    Ok(())
}
