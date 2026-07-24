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

#[derive(knus::Decode)]
pub struct Pk {
    #[knus(argument)]
    pub name: String,
    #[knus(property(name="type"))]
    pub the_type: String,
}

#[derive(knus::Decode)]
pub struct HasMany {
    #[knus(argument)]
    pub name: String,
    #[knus(property(name="as"))]
    pub the_as: String,
    #[knus(property(name="on-delete"))]
    pub on_delete: Option<String>,
    #[knus(property(name="on-update"))]
    pub on_update: Option<String>,
    #[knus(property)]
    pub optional: Option<bool>,
}

#[derive(knus::Decode)]
pub struct HasOne {
    #[knus(argument)]
    pub name: String,
    #[knus(property(name="as"))]
    pub the_as: String,
    #[knus(property(name="on-delete"))]
    pub on_delete: Option<String>,
    #[knus(property(name="on-update"))]
    pub on_update: Option<String>,
    #[knus(property)]
    pub optional: Option<bool>,
}

#[derive(knus::Decode)]
pub struct BelongsTo {
    #[knus(argument)]
    pub name: String,
    #[knus(property)]
    pub on: Option<String>,
    #[knus(property(name="as"))]
    pub the_as: String,
    #[knus(property)]
    pub optional: Option<bool>,
}

#[derive(knus::Decode)]
pub struct Bind {
    #[knus(argument)]
    pub name: String,
    #[knus(property)]
    pub from: Option<String>
}

#[derive(knus::Decode)]
pub struct Data {
    #[knus(argument)]
    pub name: String,
    #[knus(property)]
    pub query: String,
    #[knus(children)]
    pub bind: Vec<Bind>,
}

#[derive(knus::Decode)]
pub enum MethodChildren {
    Param(Param),
    Data(Data)
}

#[derive(knus::Decode)]
pub struct Get {
    #[knus(argument)]
    pub name: String,
    #[knus(property)]
    pub sql: String,
    #[knus(children)]
    pub childreen: Vec<MethodChildren>,
}

#[derive(knus::Decode)]
pub struct Post {
    #[knus(argument)]
    pub name: String,
    #[knus(property)]
    pub sql: String,
    #[knus(children)]
    pub childreen: Vec<MethodChildren>,
}

#[derive(knus::Decode)]
pub struct Put {
    #[knus(argument)]
    pub name: String,
    #[knus(property)]
    pub sql: String,
    #[knus(children)]
    pub childreen: Vec<MethodChildren>,
}

#[derive(knus::Decode)]
pub struct Delete {
    #[knus(argument)]
    pub name: String,
    #[knus(property)]
    pub sql: String,
    #[knus(children)]
    pub childreen: Vec<MethodChildren>,
}

#[derive(knus::Decode)]
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

#[derive(knus::Decode)]
pub struct Field {
    #[knus(argument)]
    pub name: String,
    #[knus(property(name="type"))]
    pub the_type: String,
    #[knus(property)]
    pub hash: Option<String>,
    #[knus(property)]
    pub optional: Option<bool>,
    #[knus(property)]
    pub unique: Option<bool>,
}

#[derive(knus::Decode)]
pub enum SchemaChild {
    Pk(Pk),
    Field(Field),
    HasMany(HasMany),
    HasOne(HasOne),
    BelongsTo(BelongsTo)
}

#[derive(knus::Decode)]
pub struct Schema {
    #[knus(children)]
    pub children: Vec<SchemaChild>,
}

#[derive(knus::Decode)]
pub enum EntityChildren {
    Schema(Schema),
    Queries(Queries)
}

#[derive(knus::Decode)]
pub struct Entity {
    #[knus(argument)]
    pub name: String,
    #[knus(children)]
    pub children: Vec<EntityChildren>,
}

#[derive(knus::Decode)]
pub struct Param {
    #[knus(argument)]
    pub name: String,
    #[knus(property(name="type"))]
    pub the_type: String,
    #[knus(property)]
    pub value: Option<String>,
    #[knus(property)]
    pub default: Option<String>,
}

#[derive(knus::Decode)]
pub struct Role {
    #[knus(argument)]
    pub name: String,
    #[knus(property(name="where"))]
    pub condition: String,
    #[knus(children)]
    pub param: Vec<Param>,
}
