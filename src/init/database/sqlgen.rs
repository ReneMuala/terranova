use crate::init::types::{Application, Entity, EntityChildren, QueriesChildren, Schema};

#[derive(Debug)]
pub enum SqlGenService<'app> {
    Crud(String, &'app Schema),
    Query(&'app QueriesChildren),
}

pub trait SqlGen {
    fn get_schema(entity: &Entity) -> Option<&Schema> {
        for child in &entity.children {
            match child {
                EntityChildren::Schema(schema) => return Some(&schema),
                _ => {}
            }
        }
        None
    }

    fn init_statements(
        &self,
        app: &Application,
    ) -> Result<Vec<String>, Box<dyn std::error::Error + Send + Sync>>;

    fn services<'a>(
        &self,
        app: &'a Application,
    ) -> Result<Vec<SqlGenService<'a>>, Box<dyn std::error::Error + Send + Sync>>;
}
