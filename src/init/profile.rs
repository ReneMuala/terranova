use std::net::SocketAddr;

use tracing::info;

use crate::init::{
    profile::Error::{MalformedListenAddress, NoSuitableProfileFound},
    types::{Application, ApplicationChildren, Profile},
};

use thiserror::Error;

#[derive(Debug)]
pub struct ProfileSpec {
    addresses: Vec<SocketAddr>,
    database: String,
}

#[derive(Debug, Error)]
pub enum Error {
    #[error("No default or name profile found")]
    NoSuitableProfileFound,
    #[error("listen address \"{0}\" is not in the format aaa.aaa.aaa.aaa")]
    MalformedListenAddress(String),
}

impl ProfileSpec {
    fn get_selected_or_default_profile<'a>(
        app: &'a Application,
        selected_profile: Option<String>,
    ) -> Option<&'a Profile> {
        for child in &app.children {
            if let ApplicationChildren::Profile(profile) = child {
                if selected_profile.is_none() && profile.default.unwrap_or(false)
                    || selected_profile.iter().all(|it| *it == profile.name)
                {
                    return Some(&profile);
                }
            }
        }
        None
    }

    pub fn new(app: &Application, selected_profile: Option<String>) -> Result<ProfileSpec, Error> {
        let mut addresses = vec![];
        let profile = ProfileSpec::get_selected_or_default_profile(app, selected_profile)
            .ok_or(NoSuitableProfileFound)?;
        for listen in &profile.listen {
            let mut ip_address: [u8; 4] = [0; 4];
            let taken = listen.address.split(".").into_iter().take(4);
            if taken.clone().count() != 4 {
                return Err(MalformedListenAddress(listen.address.clone()));
            }
            taken
                .enumerate()
                .for_each(|(k, v)| ip_address[k] = v.parse().unwrap_or_default());
            let address = (ip_address, listen.port as u16);
            // info!("listening {address:?}");
            addresses.push(SocketAddr::from(address));
        }

        Ok(ProfileSpec {
            addresses,
            database: profile
                .database
                .clone()
                .unwrap_or(format!("{}.sqlite", app.name)),
        })
    }
}
