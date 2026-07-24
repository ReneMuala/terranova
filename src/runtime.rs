use cxx::{CxxString, let_cxx_string};
use tracing::info;

#[cxx::bridge(namespace="terranova")]
mod ffi {
    unsafe extern "C++" {
        include!("terranova/src/cpp/tcc.hpp");
        fn init()->u64;
        fn deinit(ctx: u64);
        fn compile(ctx: u64, code: &CxxString) -> bool;
        fn get_callable(ctx: u64, code: &CxxString) -> u64;
        fn call(func: u64);
        // fn set_callable()
        // fn feed(code: &CxxString, callback: fn());
    }
}


pub struct JitRuntime {
    ctx: u64,
    compiled: bool
}

pub struct Callable<'a> {
    it: u64,
    rt: &'a JitRuntime
}


impl <'a> Callable<'a> {
    pub fn call(&self) {
        ffi::call(self.it);
    }
}

impl JitRuntime {
    pub fn new() -> Self {
        Self {
            ctx: ffi::init(),
            compiled: false
        }
    }

    pub fn compile(&mut self, code: &str) -> Result<(), String> {
        if self.compiled {
            return Err(String::from("already compiled"));
        }
        self.compiled = true;
        let_cxx_string!(cxx_code = code);
        if ffi::compile(self.ctx, &cxx_code) {
            Ok(())
        } else {
            Err(String::from("compilation failed"))
        }
    }

    pub fn get(&self, callable: &str) -> Option<Callable> {
        let_cxx_string!(cxx_callable = callable);
        let it = ffi::get_callable(self.ctx, &cxx_callable);
        if it != 0 {
            Some(Callable { it: it, rt: self })
        } else {
            None
        }
    }
}

impl Drop for JitRuntime  {
    fn drop(&mut self) {
        ffi::deinit(self.ctx);
    }
}

pub fn test(){

}

pub fn run(code: String) {
}
