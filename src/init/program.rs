use std::{
    collections::{HashMap, HashSet},
    fmt::format,
    hash::{DefaultHasher, Hash, Hasher},
};

use crate::init::{
    database::{
        ServiceType::{self, Update},
        SqlGenService,
    },
    program::Instruction::DeclareHandler,
    spec::Spec,
    types::Param,
};
use thiserror::Error;
use tracing::info;
#[derive(Debug)]
pub struct Program {
    code: String,
}

#[derive(Debug, Error)]
pub enum Error {
    #[error("TypeNotFound: {0}")]
    TypeNotFound(String),
    #[error("MissingDecoder")]
    MissingDecoder,
}

enum Type {
    Int,
    Float,
    Blob,
    Struct(String),
}

enum Instruction {
    Declare {
        r#type: Type,
        name: String,
    },
    Symbol(String),
    Call {
        function: Box<Instruction>,
        args: Vec<Instruction>,
    },
    If {
        condition: Box<Instruction>,
        logic: Vec<Instruction>,
    },
    Decode {
        decoder: String,
    },
    DeclareHandler {
        name: String,
        logic: Vec<Instruction>,
    },
}

#[derive(Debug)]
pub struct Endpoint {
    route: String,
    method: String,
    index: usize,
    struct_key: Option<u64>,
    decoder: Option<String>,
}

#[derive(Debug, PartialEq, Eq, Hash)]
pub struct StructField {
    name: String,
    r#type: String,
}

#[derive(Debug, PartialEq, Eq, Hash)]
pub struct Struct {
    fields: Vec<StructField>,
}

impl Struct {
    pub fn key(&self) -> u64 {
        let mut hasher = DefaultHasher::new();
        self.hash(&mut hasher);
        hasher.finish()
    }

    pub fn definition(&self) -> String {
        let key = self.key();
        let field_defs = self
            .fields
            .iter()
            .fold(String::new(), |rs, StructField { name, r#type: ty }| {
                format!("{rs} {ty} {name};")
            });
        format!("struct sk{key} {{{field_defs} }};")
    }

    pub fn from_params(params: &Vec<Param>) -> Result<Struct, Error> {
        let fields: Result<Vec<StructField>, Error> = params
            .iter()
            .map(|Param { name, r#type, .. }| {
                Ok(StructField {
                    name: name.clone(),
                    r#type: Program::get_type(r#type)?,
                })
            })
            .collect();
        let fields = fields?;
        Ok(Struct { fields })
    }
}

impl Program {
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

    fn build_handler(
        (
            Endpoint {
                route,
                method,
                index,
                struct_key,
                decoder,
            },
            query,
        ): &(Endpoint, String),
    ) -> Result<String, Error> {
        use Instruction::*;
        if let Some(key) = struct_key {
            let decoder = decoder.as_ref().ok_or(Error::MissingDecoder)?;

            let inst: Instruction = DeclareHandler {
                name: format!("h{index}"),
                logic: vec![
                    Declare {
                        r#type: Type::Struct(format!("sk{key}")),
                        name: "in".to_owned(),
                    },
                    If {
                        condition: Box::new(Call {
                            function: Box::new(Symbol(decoder.clone())),
                            args: vec![Symbol("in".to_string())],
                        }),
                        logic: vec![],
                    },
                ],
            };

            Ok(format!(
                "/* {method} {route} */ \n{query}\nvoid h{index}(obj ctx){{ if({decoder}(ctx, &in)){{}} }}"
            ))
        } else {
            Ok(format!(
                "/* {method} {route} */ \n{query}\nvoid h{index}(obj ctx){{}}"
            ))
        }
    }

    fn build_query_decoder(str: &Struct) -> (String, String) {
        let key = str.key();
        let name = format!("qd{key}");
        let code = format!("bool {name}(obj ctx, struct sk{key} * in){{}}");
        (name, code)
    }

    fn build_body_decoder(str: &Struct) -> (String, String) {
        let key = str.key();
        let name = format!("bd{key}");
        let code = format!("bool {name}(obj ctx, struct sk{key} * in){{}}");
        (name, code)
    }

    fn build_c_bytes_from_sting_query(query: &str, index: usize) -> String {
        let bytes = query.bytes().fold(String::new(), |rs, byte| {
            if rs.is_empty() {
                format!("{byte}")
            } else {
                format!("{rs}, {byte}")
            }
        });
        format!("static const char h{index}query[] = {{{bytes}}};")
    }
    pub fn typedefs() -> String {
        "typedef char * cstr;\ntypedef void* obj;\ntypedef int bool;\n#define true 1\n#define false 0\n".to_owned()
    }
    pub fn from_spec(spec: Spec) -> Result<Self, Error> {
        let mut code = String::new();
        let mut endpoints: Vec<(Endpoint, String)> = vec![];
        let mut structs: HashMap<u64, Struct> = HashMap::new();
        let mut body_decoders: HashMap<u64, String> = HashMap::new();
        let mut query_decoders: HashMap<u64, String> = HashMap::new();
        for (index, service) in spec.service.services.iter().enumerate() {
            match service {
                SqlGenService::Login { query, params } => {
                    let key = {
                        let str = Struct::from_params(params)?;
                        let key = str.key();
                        if !structs.contains_key(&key) {
                            structs.insert(key, str);
                        }
                        key
                    };
                    let route = "/login".to_owned();
                    let method = "POST".to_owned();

                    let decoder = if !body_decoders.contains_key(&key) {
                        if let Some(str) = structs.get(&key) {
                            let (name, code) = Program::build_body_decoder(str);
                            body_decoders.insert(key, code);
                            Some(name)
                        } else {
                            None
                        }
                    } else {
                        None
                    };

                    let struct_key = Some(key);
                    endpoints.push((
                        Endpoint {
                            route,
                            method,
                            index,
                            struct_key,
                            decoder,
                        },
                        Program::build_c_bytes_from_sting_query(&query, index),
                    ));
                }
                SqlGenService::Crud {
                    r#type,
                    name,
                    query,
                    params,
                    entity,
                }
                | SqlGenService::Query {
                    r#type,
                    name,
                    query,
                    params,
                    entity,
                } => {
                    let key: Option<u64> = {
                        if params.is_empty() {
                            None
                        } else {
                            let str = Struct::from_params(params)?;
                            let key = str.key();
                            if !structs.contains_key(&key) {
                                structs.insert(key, str);
                            }
                            Some(key)
                        }
                    };
                    let route = format!("/{entity}/{name}",);
                    let method = match r#type {
                        ServiceType::Create => "POST",
                        ServiceType::Read => "GET",
                        ServiceType::Update => "PUT",
                        ServiceType::Delete => "DELETE",
                    }
                    .to_owned();

                    let decoder = if let Some(key) = key {
                        match r#type {
                            ServiceType::Create | ServiceType::Update => {
                                if !body_decoders.contains_key(&key) {
                                    if let Some(str) = structs.get(&key) {
                                        let (name, code) = Program::build_body_decoder(str);
                                        body_decoders.insert(key, code);
                                        Some(name)
                                    } else {
                                        None
                                    }
                                } else {
                                    None
                                }
                            }
                            ServiceType::Read | ServiceType::Delete => {
                                if !query_decoders.contains_key(&key) {
                                    if let Some(str) = structs.get(&key) {
                                        let (name, code) = Program::build_query_decoder(str);
                                        query_decoders.insert(key, code);
                                        Some(name)
                                    } else {
                                        None
                                    }
                                } else {
                                    None
                                }
                            }
                        }
                    } else {
                        None
                    };

                    let struct_key = key;
                    endpoints.push((
                        Endpoint {
                            route,
                            method,
                            index,
                            struct_key,
                            decoder,
                        },
                        Program::build_c_bytes_from_sting_query(&query, index),
                    ));
                }
            }
        }

        code.push_str(&Program::typedefs());

        code.push_str(&structs.into_iter().fold(String::new(), |rs, st| {
            format!("{rs}{}\n", st.1.definition())
        }));

        code.push_str(
            &query_decoders
                .into_iter()
                .fold(String::new(), |rs, dq| format!("{rs}{}\n", dq.1)),
        );

        code.push_str(
            &body_decoders
                .into_iter()
                .fold(String::new(), |rs, dq| format!("{rs}{}\n", dq.1)),
        );

        code.push_str(&endpoints.iter().map(Program::build_handler).fold(
            String::new(),
            |rs, h| match h {
                Ok(h) => format!("{rs}{h}\n"),
                Err(_) => format!("{rs}\n"),
            },
        ));

        info!("code: \n{code}");

        std::fs::write("jit.c", &code).expect("failed to write file");

        Ok(Program { code })
    }
}
