use crate::init::types::Application;

#[derive(Debug)]
pub struct Metadata {
    pub name: String,
    pub license: Option<String>,
    pub author: Option<String>,
    pub version: Option<String>,
    pub email: Option<String>,
}

impl Metadata {
    pub fn new(
        Application {
            name,
            license,
            author,
            version,
            email,
            ..
        }: &Application,
    ) -> Metadata {
        Metadata {
            name: name.clone(),
            license: license.clone(),
            author: author.clone(),
            version: version.clone(),
            email: email.clone(),
        }
    }
}
