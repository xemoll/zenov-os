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

bool path_within(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    auto root_it = root.begin();
    auto candidate_it = candidate.begin();
    for (; root_it != root.end(); ++root_it, ++candidate_it) {
        if (candidate_it == candidate.end() || *root_it != *candidate_it) return false;
    }
    return true;
}

bool parse_include(const std::string& text, std::string& relative) {
    constexpr const char* prefix = "include(\"";
    constexpr const char* suffix = "\");";
    if (text.size() < 13 || text.compare(0, 9, prefix) != 0 ||
        text.compare(text.size() - 3, 3, suffix) != 0) return false;
    relative = text.substr(9, text.size() - 12);
    return !relative.empty();
}

std::string trim_copy(const std::string& input) {
    std::size_t first = 0;
    while (first < input.size() && std::isspace(static_cast<unsigned char>(input[first]))) ++first;
    std::size_t last = input.size();
    while (last > first && std::isspace(static_cast<unsigned char>(input[last - 1]))) --last;
    return input.substr(first, last - first);
}

void expand_file(const std::filesystem::path& file,
                 const std::filesystem::path& root,
                 std::unordered_set<std::string>& active,
                 std::ostringstream& output,
                 std::size_t depth) {
    if (depth > 16) throw Error("include depth exceeds 16 at " + file.string());
    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(file, ec);
    if (ec) throw Error("cannot resolve include: " + file.string());
    if (!path_within(root, canonical)) throw Error("include escapes source root: " + canonical.string());
    const std::string key = canonical.generic_string();
    if (!active.insert(key).second) throw Error("include cycle detected at " + key);

    std::istringstream input(read_file(canonical.string()));
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const std::string trimmed = trim_copy(line);
        std::string relative;
        if (parse_include(trimmed, relative)) {
            const std::filesystem::path include_path(relative);
            if (include_path.is_absolute()) throw Error("absolute include is forbidden at " + key + ":" + std::to_string(line_number));
            expand_file(canonical.parent_path() / include_path, root, active, output, depth + 1);
        } else {
            output << line << '\n';
        }
    }
    active.erase(key);
}

Config compile_file(const std::string& path) {
    std::error_code ec;
    const auto input = std::filesystem::weakly_canonical(path, ec);
    if (ec) throw Error("cannot resolve input: " + path);
    const auto root = input.parent_path();
    std::unordered_set<std::string> active;
    std::ostringstream expanded;
    expand_file(input, root, active, expanded, 0);
    return compile_source(expanded.str());
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
  shell_limits(512, 128);
  boot_message("boot");
  shell_command("about", "ok");
  vfs_file("README", "text\n");
}
)zv";
    const Config config = compile_source(source);
    const std::string header = generate(config);
    if (header.find("kVersion[] = \"0.1.1\"") == std::string::npos) throw Error("self-test generated version missing");
    if (header.find("kShellLineCapacity = 512") == std::string::npos) throw Error("self-test shell line capacity missing");
    if (header.find("kShellHistoryCapacity = 128") == std::string::npos) throw Error("self-test shell history capacity missing");
    if (header.find("{\"README\", \"text\\n\"}") == std::string::npos) throw Error("self-test generated VFS missing");
    if (generate(config) != header) throw Error("self-test generation is not deterministic");

    std::ostringstream large;
    large << "fn main(){system_name(\"ZenovOS\");system_version(\"0.1.1\");shell_prompt(\"z> \");shell_limits(512,128);boot_message(\"boot\");vfs_file(\"R\",\"x\");";
    for (int i = 0; i < 200; ++i) large << "shell_command(\"c" << i << "\",\"ok\");";
    large << "}";
    const Config large_config = compile_source(large.str());
    if (large_config.commands.size() != 200) throw Error("self-test large source did not preserve 200 commands");

    const auto temp_root = std::filesystem::temp_directory_path() / "zenov-stage0-self-test";
    std::error_code ec;
    std::filesystem::remove_all(temp_root, ec);
    std::filesystem::create_directories(temp_root / "config", ec);
    if (ec) throw Error("self-test cannot create temporary source tree");
    write_file((temp_root / "main.zv").string(), "fn main(){\ninclude(\"config/base.zv\");\ninclude(\"config/files.zv\");\n}\n");
    write_file((temp_root / "config/base.zv").string(), "system_name(\"ZenovOS\");\nsystem_version(\"0.1.1\");\nshell_prompt(\"z> \" );\nshell_limits(512,128);\nboot_message(\"boot\");\n");
    write_file((temp_root / "config/files.zv").string(), "include(\"nested.zv\");\n");
    write_file((temp_root / "config/nested.zv").string(), "vfs_file(\"README\",\"recursive include ok\");\n");
    const Config included = compile_file((temp_root / "main.zv").string());
    if (included.files.size() != 1 || included.files[0].second != "recursive include ok") throw Error("self-test recursive include failed");
    std::filesystem::remove_all(temp_root, ec);

    expect_failure("fn main(){system_name(\"ZenovOS\");system_version(\"0.1.0\");shell_prompt(\"x\");boot_message(\"x\");vfs_file(\"R\",\"x\");}", "0.1.1");
    expect_failure("fn main(){system_name(\"ZenovOS\");system_version(\"0.1.1\");shell_prompt(\"x\");boot_message(\"x\");vfs_file(\"R\",\"x\");unknown();}", "unsupported");
    expect_failure("fn main(){system_name(\"ZenovOS\");system_version(\"0.1.1\");shell_prompt(\"x\");boot_message(\"x\");vfs_file(\"R\",\"x\");vfs_file(\"R\",\"y\");}", "duplicate");
    expect_failure("fn main(){system_name(\"ZenovOS\");system_version(\"0.1.1\");shell_prompt(\"x\");shell_limits(64,16);boot_message(\"x\");vfs_file(\"R\",\"x\");}", "128..1024");
    std::cout << "zenov stage0 self-test: OK version=0.1.1 statements=200 includes=recursive shell=512x128\n";
}
