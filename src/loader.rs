use std::{fs::File, io::Read, panic, print};

use tracing::info;
use crate::types::Application;

fn load_file_or_panic(filename: &str) -> String {
    match &mut File::open(filename) {
        Ok(file) => {
            let mut buff = String::new();
            file.read_to_string(&mut buff).unwrap();
            buff
        },
        Err(err) => panic!("{}: {}", err, filename),
    }
}

pub fn load_or_panic(filename: &str) -> Option<Vec<Application>>{
    let content = load_file_or_panic(filename);
    info!("content: {}", content);
    match knus::parse::<Vec<Application>>("Test", &content) {
        Ok(app) => Some(app),
        Err(err) => {
            print!("{:?}", miette::Report::new(err));
            None
        },
    }
}
