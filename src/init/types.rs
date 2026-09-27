#[derive(knus::Decode)]
pub struct Application {
    #[knus(argument)]
    pub name: String,
    #[knus(property)]
    pub license: Option<String>,
    #[knus(property)]
    pub author: Option<String>,
    #[knus(property)]
    pub version: Option<String>,
    #[knus(property)]
    pub email: Option<String>,
    #[knus(property)]
    pub namespace: Option<String>,
    #[knus(property)]
    pub serve_docs: Option<bool>,
    #[knus(child)]
    pub auth: Option<Auth>,
    #[knus(children)]
    pub children: Vec<ApplicationChildren>,
}

#[derive(knus::Decode)]
pub enum ApplicationChildren {
    Profile(Profile),
    Entity(Entity),
}

#[derive(knus::Decode)]
pub struct Profile {
    #[knus(argument)]
    pub name: String,
    #[knus(property)]
    pub default: Option<bool>,
    #[knus(property)]
    pub database: Option<String>,
    #[knus(children)]
    pub listen: Vec<Listen>,
}

#[derive(knus::Decode)]
pub struct Listen {
    #[knus(property)]
    pub address: String,
    #[knus(property)]
    pub port: i16,
    #[knus(property)]
    pub domain: Option<String>,
    #[knus(property)]
    pub cert: Option<String>,
    #[knus(property)]
    pub key: Option<String>,
}

#[derive(knus::Decode)]
pub struct On {
    #[knus(argument)]
    pub status: String,
}

#[derive(knus::Decode)]
pub struct Redirect {
    #[knus(argument)]
    pub to: String,
    #[knus(children)]
    pub on: Vec<On>,
}

#[derive(knus::Decode)]
pub struct Auth {
    #[knus(property)]
    pub provider: String,
    #[knus(property)]
    pub identity: String,
    #[knus(property)]
    pub secret: String,
    #[knus(property)]
    pub timeout: Option<u64>,
    #[knus(property)]
    pub persist: Option<u64>,
    #[knus(children)]
    pub redirect: Vec<Redirect>,
}

#[derive(knus::Decode, Clone, Debug)]
pub struct Pk {
    #[knus(argument)]
    pub name: String,
    #[knus(property(name = "type"))]
    pub the_type: String,
}

#[derive(knus::Decode, Clone, Debug)]
pub struct HasMany {
    #[knus(argument)]
    pub name: String,
    #[knus(property(name = "as"))]
    pub the_as: String,
    #[knus(property(name = "on-delete"))]
    pub on_delete: Option<String>,
    #[knus(property(name = "on-update"))]
    pub on_update: Option<String>,
    #[knus(property)]
    pub optional: Option<bool>,
}

#[derive(knus::Decode, Clone, Debug)]
pub struct HasOne {
    #[knus(argument)]
    pub name: String,
    #[knus(property(name = "as"))]
    pub the_as: String,
    #[knus(property(name = "on-delete"))]
    pub on_delete: Option<String>,
    #[knus(property(name = "on-update"))]
    pub on_update: Option<String>,
    #[knus(property)]
    pub optional: Option<bool>,
}

#[derive(knus::Decode, Clone, Debug)]
pub struct BelongsTo {
    #[knus(argument)]
    pub entity: String,
    #[knus(property)]
    pub on: Option<String>,
    #[knus(property(name = "as"))]
    pub the_as: Option<String>,
    #[knus(property)]
    pub optional: Option<bool>,
}

impl BelongsTo {
    pub fn name(&self) -> String {
        format!("{}_id", self.the_as.clone().unwrap_or(self.entity.clone()))
    }
}

#[derive(knus::Decode, Debug)]
pub struct Bind {
    #[knus(argument)]
    pub name: String,
    #[knus(property)]
    pub from: Option<String>,
}

#[derive(knus::Decode, Debug)]
pub struct Data {
    #[knus(argument)]
    pub name: String,
    #[knus(property)]
    pub query: String,
    #[knus(children)]
    pub bind: Vec<Bind>,
}

#[derive(knus::Decode, Debug)]
pub enum MethodChildren {
    Param(Param),
    Data(Data),
}

#[derive(knus::Decode, Debug)]
pub struct Get {
    #[knus(argument)]
    pub name: String,
    #[knus(property)]
    pub sql: String,
    #[knus(children)]
    pub childreen: Vec<MethodChildren>,
}

#[derive(knus::Decode, Debug)]
pub struct Post {
    #[knus(argument)]
    pub name: String,
    #[knus(property)]
    pub sql: String,
    #[knus(children)]
    pub childreen: Vec<MethodChildren>,
}

#[derive(knus::Decode, Debug)]
pub struct Put {
    #[knus(argument)]
    pub name: String,
    #[knus(property)]
    pub sql: String,
    #[knus(children)]
    pub childreen: Vec<MethodChildren>,
}

#[derive(knus::Decode, Debug)]
pub struct Delete {
    #[knus(argument)]
    pub name: String,
    #[knus(property)]
    pub sql: String,
    #[knus(children)]
    pub childreen: Vec<MethodChildren>,
}

#[derive(knus::Decode, Debug)]
pub enum QueriesChildren {
    Get(Get),
    Post(Post),
    Put(Put),
    Delete(Delete),
}

#[derive(knus::Decode)]
pub struct Queries {
    #[knus(children)]
    pub children: Vec<QueriesChildren>,
}

#[derive(knus::Decode, Clone, Debug)]
pub struct Field {
    #[knus(argument)]
    pub name: String,
    #[knus(property(name = "type"))]
    pub the_type: String,
    #[knus(property)]
    pub hash: Option<String>,
    #[knus(property)]
    pub optional: Option<bool>,
    #[knus(property)]
    pub unique: Option<bool>,
}

#[derive(knus::Decode, Clone, Debug)]
pub enum SchemaChild {
    Pk(Pk),
    Field(Field),
    HasMany(HasMany),
    HasOne(HasOne),
    BelongsTo(BelongsTo),
}

impl SchemaChild {
    pub fn name_is(&self, candidate: &str) -> bool {
        match self {
            SchemaChild::Pk(Pk { name, .. })
            | SchemaChild::Field(Field { name, .. })
            | SchemaChild::HasMany(HasMany { name, .. })
            | SchemaChild::HasOne(HasOne { name, .. }) => name == candidate,
            SchemaChild::BelongsTo(belongs_to) => belongs_to.name() == candidate,
        }
    }
}

#[derive(knus::Decode, Debug)]
pub struct Schema {
    #[knus(children)]
    pub children: Vec<SchemaChild>,
}

#[derive(knus::Decode)]
pub enum EntityChildren {
    Schema(Schema),
    Queries(Queries),
}

#[derive(knus::Decode)]
pub struct Entity {
    #[knus(argument)]
    pub name: String,
    #[knus(children)]
    pub children: Vec<EntityChildren>,
}

#[derive(knus::Decode, Debug)]
pub struct Param {
    #[knus(argument)]
    pub name: String,
    #[knus(property(name = "type"))]
    pub the_type: String,
    #[knus(property)]
    pub value: Option<String>,
    #[knus(property)]
    pub default: Option<String>,
}

impl Param {
    pub fn exposed(&self) -> bool {
        self.value.is_none()
    }
}

#[derive(knus::Decode)]
pub struct Role {
    #[knus(argument)]
    pub name: String,
    #[knus(property(name = "where"))]
    pub condition: String,
    #[knus(children)]
    pub param: Vec<Param>,
}
