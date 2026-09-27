use crate::init::types::{Data, Param};

pub enum DataProvider {
    url_param,
    request_body,
}

pub enum Kind {
    business,
    login,
    logout,
    role,
}

pub struct Service {
    pub route: String,
    pub method: String,
    pub access: String,
    pub provider: DataProvider,
    pub kind: Kind,
    pub statement: Option<String>,
    pub params: Vec<Param>,
    pub data: Vec<Data>,
}

impl Service {
    pub fn composed(&self) -> bool {
        self.statement.is_none()
    }
}
