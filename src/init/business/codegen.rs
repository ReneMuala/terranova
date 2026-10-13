use crate::init::Spec;

pub enum Method {
    Get,
    Post,
    Put,
    Delete,
}

pub struct Endpoint {
    route: String,
    method: Method,
    handler: String,
}

pub struct Program {
    spec: Spec,
    code: String,
    endpoints: Vec<Endpoint>,
}

pub trait CompiledProgram {
    fn function(&self) -> u64;
}

pub trait CodeGen {
    fn compile(
        &self,
        program: Program,
    ) -> Result<Box<dyn CompiledProgram>, Box<dyn std::error::Error + Send + Sync>>;
    fn generate(&self, spec: Spec) -> Result<Program, Box<dyn std::error::Error + Send + Sync>>;
}
