Config compile_source(const std::string& source) {
    return validate(Parser(Lexer(source).run()).parse());
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw Error("cannot open input: " + path);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void write_file(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw Error("cannot open output: " + path);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out) throw Error("failed writing output: " + path);
}

void expect_failure(const std::string& source, const std::string& needle) {
    try {
        (void)compile_source(source);
    } catch (const Error& error) {
        if (std::string(error.what()).find(needle) == std::string::npos) {
            throw Error("self-test expected '" + needle + "', got '" + error.what() + "'");
        }
        return;
    }
    throw Error("self-test expected failure containing: " + needle);
}

void self_test() {
    const std::string source = R"zv(
fn main() {
  system_name("ZenovOS");
  system_version("0.1.1");
  shell_prompt("z> ");
  theme(10, 0);
  boot_message("boot");
  shell_command("about", "ok");
  vfs_file("README", "text\n");
}
)zv";
    const Config config = compile_source(source);
    const std::string header = generate(config);
    if (header.find("kVersion[] = \"0.1.1\"") == std::string::npos) throw Error("self-test generated version missing");
    if (header.find("{\"README\", \"text\\n\"}") == std::string::npos) throw Error("self-test generated VFS missing");
    if (generate(config) != header) throw Error("self-test generation is not deterministic");
    expect_failure("fn main(){system_name(\"ZenovOS\");system_version(\"0.1.0\");shell_prompt(\"x\");boot_message(\"x\");vfs_file(\"R\",\"x\");}", "0.1.1");
    expect_failure("fn main(){system_name(\"ZenovOS\");system_version(\"0.1.1\");shell_prompt(\"x\");boot_message(\"x\");vfs_file(\"R\",\"x\");unknown();}", "unsupported");
    expect_failure("fn main(){system_name(\"ZenovOS\");system_version(\"0.1.1\");shell_prompt(\"x\");boot_message(\"x\");vfs_file(\"R\",\"x\");vfs_file(\"R\",\"y\");}", "duplicate");
    std::cout << "zenov stage0 self-test: OK version=0.1.1\n";
}
