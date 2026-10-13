use crate::init::{
    Spec,
    business::{
        c::tcc::Struct,
        codegen::{CodeGen, CompiledProgram, Program},
    },
    database::SqlGenService,
};
use std::collections::HashMap;
use thiserror::Error;

pub struct C;
#[derive(Debug, Error)]
pub enum Error {
    #[error("TypeNotFound: {0}")]
    TypeNotFound(String),
    #[error("MissingDecoder")]
    MissingDecoder,
}

impl CodeGen for C {
    fn compile(
        &self,
        program: Program,
    ) -> Result<Box<dyn CompiledProgram>, Box<dyn std::error::Error + Send + Sync>> {
        todo!()
    }

    fn generate(&self, spec: Spec) -> Result<Program, Box<dyn std::error::Error + Send + Sync>> {
        let mut code = String::new();
        let mut structs: HashMap<u64, Struct> = HashMap::new();
        for (index, service) in spec.service.services.iter().enumerate() {
            let (query, params) = service.values();
            let method = service.method();
            let entity = service.entity();
            let key = if !params.is_empty() {
                let r#struct = Struct::from_params(&params)?;
                let key = r#struct.key();
                structs.insert(key, r#struct);
                Some(key)
            } else {
                None
            };
            // create the thing
        }
        code.push_str(
            &structs
                .iter_mut()
                .fold(String::new(), |rs, it| format!("{rs}{}", it.1.generate())),
        );
        todo!()
    }
}

mod tcc {
    use std::hash::{DefaultHasher, Hash, Hasher};

    use crate::init::{business::c::Error, types::Param};

    #[derive(Debug, PartialEq, Eq, Hash)]
    pub struct Field {
        name: String,
        r#type: String,
    }

    #[derive(Debug, PartialEq, Eq, Hash)]
    pub struct Struct {
        fields: Vec<Field>,
    }

    impl Struct {
        pub fn key(&self) -> u64 {
            let mut hasher = DefaultHasher::new();
            self.hash(&mut hasher);
            hasher.finish()
        }

        pub fn generate(&self) -> String {
            let key = self.key();
            let field_defs = self
                .fields
                .iter()
                .fold(String::new(), |rs, Field { name, r#type: ty }| {
                    format!("{rs} {ty} {name};")
                });
            format!("struct sk{key} {{{field_defs} }};")
        }

        pub fn from_params(params: &Vec<Param>) -> Result<Struct, Error> {
            let fields: Result<Vec<Field>, Error> = params
                .iter()
                .map(|Param { name, r#type, .. }| {
                    Ok(Field {
                        name: name.clone(),
                        r#type: get_type(r#type)?,
                    })
                })
                .collect();
            let fields = fields?;
            Ok(Struct { fields })
        }
    }

    fn get_type(r#type: &str) -> Result<String, Error> {
        let stype = match r#type {
            "int" => "int",
            "float" => "float",
            "string" => "cstr",
            "blob" => "cstr",
            "file" => "cstr",
            "date" => "cstr",
            "datetime" => "cstr",
            "bool" => "bool",
            _ => "",
        }
        .to_string();

        if stype.is_empty() {
            Err(Error::TypeNotFound(r#type.to_string()))
        } else {
            Ok(stype)
        }
    }
}
