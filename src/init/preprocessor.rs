use super::types::ApplicationChildren;
use crate::init::{
    preprocessor::{
        Error::{InvalidDbName, InvalidListenIpv4Address, InvalidName},
        LocationPiece::{Borrowed, Owned},
    },
    types::{Application, Listen, Profile},
};
use regex::Regex;
use thiserror::Error;

#[derive(Debug, Clone)]
pub enum LocationPiece<'a> {
    Owned(String),
    Borrowed(&'a str),
}

impl<'a> LocationPiece<'a> {
    pub fn str(&self) -> &str {
        match self {
            LocationPiece::Owned(str) => &str,
            LocationPiece::Borrowed(str) => str,
        }
    }
}

#[derive(Debug, Clone)]
pub struct Location<'a>(Vec<LocationPiece<'a>>);

#[derive(Debug)]
pub struct LocationString(String);

impl From<Location<'_>> for LocationString {
    fn from(value: Location<'_>) -> Self {
        let mut result = String::new();
        for i in value.0 {
            let str = i.str();
            result.push_str(&format!("{str}/"));
        }
        result.pop();

        LocationString(result)
    }
}

macro_rules! with_location {
    ($current:expr, $sc:tt, $loc:tt) => {{
        let cpy = $current;
        $loc.0.push(cpy);
        let result = { $sc };
        $loc.0.pop();
        result
    }};
}

macro_rules! regex_validate_option_or {
    ($regex:tt, $field:expr,  $error:tt, $loc:tt) => {{
        let regex = Regex::new($regex)?;
        if let Some(it) = $field {
            if !regex.is_match(it) {
                return Err($error($loc.clone().into(), it.clone()));
            }
        }
    }};
}

macro_rules! regex_validate_or {
    ($regex:tt, $field:expr,  $error:tt, $loc:tt) => {{
        let regex = Regex::new($regex)?;
        if !regex.is_match($field) {
            return Err($error($loc.clone().into(), $field.clone()));
        }
    }};
}

macro_rules! trim_opt {
    ($it: expr) => {
        $it.iter_mut().for_each(|it| *it = it.trim().to_owned())
    };
}

#[derive(Debug, Error)]
pub enum Error {
    #[error("InteralRegexError")]
    InteralRegexError(#[from] regex::Error),
    #[error("InvalidDbName {1} at {0:?}")]
    InvalidDbName(LocationString, String),
    #[error("InvalidName {1} at {0:?}")]
    InvalidName(LocationString, String),
    #[error("InvalidListenIpv4Address {1} at {0:?}")]
    InvalidListenIpv4Address(LocationString, String),
}

pub fn process_profile_listen(
    location: &mut Location,
    listen: &mut Listen,
    index: usize,
) -> Result<(), Error> {
    with_location!(
        Owned(format!("listen[{index}]")),
        {
            // regex_validate_option_or!(r#"\w+\.sqlite"#, &profile.database, InvalidDbName, location);
            regex_validate_or!(
                r#"^((?:(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)\.){3}(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)|localhost)$"#,
                &listen.address,
                InvalidListenIpv4Address,
                location
            );
            trim_opt!(listen.cert);
            trim_opt!(listen.domain);
            trim_opt!(listen.key);
            Ok(())
        },
        location
    )
}

pub fn process_profile(
    location: &mut Location,
    profile: &mut Profile,
    index: usize,
) -> Result<(), Error> {
    with_location!(
        Owned(format!("profile${index}")),
        {
            regex_validate_option_or!(
                r#"^(\w+\.sqlite)$"#,
                &profile.database,
                InvalidDbName,
                location
            );
            regex_validate_or!(r#"^(\w+)$"#, &profile.name, InvalidName, location);
            for (index, listen) in profile.listen.iter_mut().enumerate() {
                process_profile_listen(location, listen, index)?;
            }
            Ok(())
        },
        location
    )
}

pub fn process(mut app: Application) -> Result<Application, Error> {
    let mut location = Location(vec![Borrowed("app")]);
    for (index, child) in app.children.iter_mut().enumerate() {
        match child {
            ApplicationChildren::Profile(profile) => {
                process_profile(&mut location, profile, index)?
            }
            ApplicationChildren::Entity(entity) => {}
        }
    }
    Ok(app)
}
