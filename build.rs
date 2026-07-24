fn main() {
    cxx_build::bridge("src/runtime.rs")
        .file("src/cpp/tcc.cpp")
        .std("c++17")
        .flags(["-ltcc"])
        .compile("cxxbridge-demo");

    println!("cargo:rerun-if-changed=src/cpp/tcc.cc");
    println!("cargo:rerun-if-changed=src/cpp/tcc.hpp");

    println!("cargo:rustc-link-search=native=/usr/local/lib");
    println!("cargo:rustc-link-lib=static=tcc");
}
