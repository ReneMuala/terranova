use crate::init::{
    Spec,
    business::{
        DecoderType,
        c::tcc::Struct,
        codegen::{CodeGen, CompiledProgram, Endpoint, Program},
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
        let mut query_decoders: HashMap<u64, String> = HashMap::new();
        let mut body_decoders: HashMap<u64, String> = HashMap::new();
        let mut endpoints: Vec<Endpoint> = vec![];
        let mut handlers: Vec<String> = vec![];
        for (index, service) in spec.service.services.iter().enumerate() {
            let (query, params) = service.query_and_params();
            let method = service.method();
            let route = service.route();

            let decoder_type = service.decoder_type();
            if !params.is_empty() {
                let r#struct = Struct::from_params(&params)?;
                let key = r#struct.key();
                tcc::generate_and_register_decoder_if_missing(
                    key,
                    (&r#struct, &decoder_type),
                    &mut query_decoders,
                    &mut body_decoders,
                );
                let (endpoint, handler) =
                    tcc::generate_endpoint(index, query, method, route, &r#struct, &decoder_type);
                endpoints.push(endpoint);
                handlers.push(handler);
                structs.insert(key, r#struct);
            }
        }
        code.push_str(
            &structs
                .iter_mut()
                .fold(String::new(), |rs, it| format!("{rs}{}", it.1.generate())),
        );
        code.push_str(
            &query_decoders
                .iter()
                .chain(body_decoders.iter())
                .fold(String::new(), |rs, (_, decoder)| format!("{rs}{decoder}")),
        );
        code.push_str(
            &handlers
                .iter_mut()
                .fold(String::new(), |rs, it| format!("{rs}{it}")),
        );
        todo!()
    }
}

mod tcc {
    use std::{
        collections::HashMap,
        hash::{DefaultHasher, Hash, Hasher},
    };

    use crate::init::{
        business::{DecoderType, Method, c::Error, codegen::Endpoint},
        types::Param,
    };

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

    fn generate_query_decoder(r#struct: &Struct) -> String {
        format!("...")
    }

    fn generate_body_decoder(r#struct: &Struct) -> String {
        format!("...")
    }

    pub fn generate_and_register_decoder_if_missing(
        key: u64,
        (r#struct, r#type): (&Struct, &DecoderType),
        query_decoders: &mut HashMap<u64, String>,
        body_decoders: &mut HashMap<u64, String>,
    ) {
        let decoders = match r#type {
            DecoderType::Body => body_decoders,
            DecoderType::Query => query_decoders,
        };

        if !decoders.contains_key(&key) {
            match r#type {
                DecoderType::Body => {
                    decoders.insert(key, generate_body_decoder(r#struct));
                }
                DecoderType::Query => {
                    decoders.insert(key, generate_query_decoder(r#struct));
                }
            }
        }
    }

    pub fn generate_endpoint(
        index: usize,
        query: &str,
        method: Method,
        route: String,
        r#struct: &Struct,
        r#type: &DecoderType,
    ) -> (Endpoint, String) {
        (
            Endpoint {
                handler: format!("test"),
                method: method,
                route: route,
            },
            format!("..."),
        )
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
