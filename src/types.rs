#[derive(knus::Decode)]
pub struct Application {
    #[knus(argument)]
    pub name: String,
    #[knus(property)]
    pub license: String,
    #[knus(property)]
    pub author: Option<String>,
    #[knus(property)]
    pub version: String,
    #[knus(property)]
    pub email: String,
    #[knus(property)]
    pub namespace: Option<String>,
    #[knus(property)]
    pub comments: Option<String>,
    #[knus(property)]
    pub serve_docs: Option<bool>,
    #[knus(child)]
    pub auth: Auth,
    #[knus(children)]
    pub entity: Vec<Entity>,
    #[knus(children)]
    pub profile: Vec<Profile>,
}


#[derive(knus::Decode)]
pub struct Proflle {
    #[knus(argument)]
    pub name: String,
    #[knus(property)]
    pub default: Option<bool>,
    #[knus(children)]
    pub listen: Vec<Listen>,
}

#[derive(knus::Decode)]
pub struct Listen {
    #[knus(argument)]
    pub name: String,
    #[knus(property)]
    pub default: Option<bool>
}