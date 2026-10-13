use std::{fs::File, io::Read};

use thiserror::Error;

use crate::init::{loader::Error::KnusError, types::Application};
#[derive(Debug, Error)]
pub enum Error {
    #[error("IoError: {0}")]
    IoError(#[from] std::io::Error),
    // #[error("KnusError: {0}")]
    // KnusError(#[from] knus::Error),
    #[error("KnusError")]
    KnusError,
}

fn load_file(filename: &str) -> Result<String, Error> {
    let mut buff = String::new();
    let file = &mut File::open(filename)?;
    file.read_to_string(&mut buff)?;
    Ok(buff)
}

pub fn load(filename: &str) -> Result<Vec<Application>, Error> {
    let content = load_file(filename)?;
    match knus::parse::<Vec<Application>>(filename, &content) {
        Ok(config) => Ok(config),
        Err(e) => {
            println!("{:?}", miette::Report::new(e));
            Err(KnusError)
        }
    }
}
