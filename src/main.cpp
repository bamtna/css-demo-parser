#include "Demo/Demo.h"
#include "Dumper.h"
#include "Util/BitReader.h"
#include <iostream>
#include <string>
#include <fstream>

class StreamRedirect {
public:
    StreamRedirect(const std::string& output_path) {
        file_stream.open(output_path);

        old_cout = std::cout.rdbuf(file_stream.rdbuf());
        old_cerr = std::cerr.rdbuf(file_stream.rdbuf());
    }

    ~StreamRedirect() {
        std::cout.rdbuf(old_cout);
        std::cerr.rdbuf(old_cerr);
    }

private:
    std::ofstream file_stream;
    std::streambuf* old_cout, * old_cerr;
};

std::string demo_path_to_dump_path(std::string demo_path) {
    size_t last_period_pos = demo_path.find_last_of('.');
    if (last_period_pos != std::string::npos && last_period_pos != 0) {
        demo_path = demo_path.substr(0, last_period_pos);
    }
    demo_path += "_dump.txt";
    return demo_path;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <demo_file_path>" << std::endl;
        return 1;
    }

    std::string demo_file_path = argv[1];
    auto dump_path = demo_path_to_dump_path(demo_file_path);
    std::string log_path = "log_" + dump_path;

    StreamRedirect redirect(log_path);

    Demo demo;
    try {
        demo.load(demo_file_path);
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to parse demo file: " << e.what() << std::endl;
        return 1;
    }

    
    Dumper dumper(demo);
    dumper.open(dump_path);
    dumper.dump_header();
    dumper.close();

    std::cout << "Successfully dumped to: " << dump_path << std::endl;

    return 0;
}