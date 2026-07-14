int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--self-test") {
            self_test();
            return 0;
        }
        if (argc != 4 || std::string(argv[2]) != "-o") {
            std::cerr << "usage: zenov-stage0 <input.zv> -o <generated.hpp>\n"
                      << "       zenov-stage0 --self-test\n";
            return 2;
        }
        const Config config = compile_source(read_file(argv[1]));
        write_file(argv[3], generate(config));
        std::cout << "zenov-stage0: generated " << argv[3] << " from " << argv[1] << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zenov-stage0: error: " << error.what() << "\n";
        return 1;
    }
}
