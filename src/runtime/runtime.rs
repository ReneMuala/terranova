use std::{marker::PhantomData, sync::Arc};

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
        fn call_ret_str(func: u64)->UniquePtr<CxxString>;
        fn call_ret_str_str(func: u64, route: &CxxString)->UniquePtr<CxxString>;
        fn call_ret_str_str_str(func: u64, route: &CxxString, body: &CxxString)->UniquePtr<CxxString>;
        // call_ret_str_str_str
        // fn set_callable()
        // fn feed(code: &CxxString, callback: fn());
    }
}


pub struct JitRuntime {
    ctx: u64,
    compiled: bool
}

pub struct Callable {
    it: u64,
    rt: Arc<JitRuntime>
}


impl Callable {
    pub unsafe fn call(&self) {
        ffi::call(self.it);
    }

    pub unsafe fn call_ret_str(&self) -> String {
        ffi::call_ret_str(self.it).to_string()
    }

    pub unsafe fn call_ret_str_str(&self, route: String) -> String {
        let_cxx_string!(rt = route);
        ffi::call_ret_str_str(self.it, &rt).to_string()
    }

    pub unsafe fn call_ret_str_str_str(&self, route: String, body: String) -> String {
        let_cxx_string!(rt = route);
        let_cxx_string!(bd = body);
        ffi::call_ret_str_str_str(self.it, &rt, &bd).to_string()
    }
}

impl Clone for Callable {
    fn clone(&self) -> Self {
        Callable { it: self.it, rt: Arc::clone(&self.rt) }
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

    pub fn get(self: &Arc<Self>, callable: &str) -> Option<Callable> {
        let_cxx_string!(cxx_callable = callable);
        let it = ffi::get_callable(self.ctx, &cxx_callable);
        if it != 0 {
            Some(Callable { it: it, rt: Arc::clone(self) })
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
