#include "cssdemo/Demo/DemoFile.h"
#include "cssdemo/Output/TextDumper.h"
#include "cssdemo/Parser/DemoParser.h"
#include <fstream>
#include <iostream>
#include <string>

using namespace cssdemo;

std::string demo_path_to_dump_path(std::string demo_path) {
    size_t last_period_pos = demo_path.find_last_of('.');
    size_t last_slash_pos = demo_path.find_last_of("/\\");
    if (last_period_pos != std::string::npos && last_period_pos != 0
        && (last_slash_pos == std::string::npos || last_period_pos > last_slash_pos)) {
        demo_path = demo_path.substr(0, last_period_pos);
    }
    demo_path += "_dump.txt";
    return demo_path;
}

void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " <demo_file_path> [options]\n"
        << "Options:\n"
        << "  -o <path>         Output file (default: <demo>_dump.txt, - for stdout)\n"
        << "  --summary         Only the header, server, players and errors, no messages\n"
        << "  --entities        Include every entity property change\n"
        << "  --tempentities    Include temp entity events\n"
        << "  --sounds          Include sounds\n"
        << "  --usercmds        Include the recording player's user commands\n"
        << "  --datatables      Include server classes and their networked props\n"
        << "  --stringtables    Include the contents of every string table\n"
        << "  --all             Include all of the above\n";
}

int main(int argc, char* argv[]) {
    std::string demo_file_path;
    std::string dump_path;
    DumpOptions options;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            dump_path = argv[++i];
        }
        else if (arg == "--summary") {
            options.messages = false;
        }
        else if (arg == "--entities") {
            options.entities = true;
        }
        else if (arg == "--tempentities") {
            options.temp_entities = true;
        }
        else if (arg == "--sounds") {
            options.sounds = true;
        }
        else if (arg == "--usercmds") {
            options.user_cmds = true;
        }
        else if (arg == "--datatables") {
            options.data_tables = true;
        }
        else if (arg == "--stringtables") {
            options.string_tables = true;
        }
        else if (arg == "--all") {
            options.entities = options.temp_entities = options.sounds = options.user_cmds = true;
            options.data_tables = options.string_tables = true;
        }
        else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        else if (!arg.empty() && arg[0] != '-' && demo_file_path.empty()) {
            demo_file_path = arg;
        }
        else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (demo_file_path.empty()) {
        print_usage(argv[0]);
        return 1;
    }
    if (dump_path.empty()) {
        dump_path = demo_path_to_dump_path(demo_file_path);
    }

    try {
        DemoFile demo(demo_file_path);

        std::ofstream file;
        if (dump_path != "-") {
            file.open(dump_path);
            if (!file) {
                std::cerr << "Failed to open output file: " << dump_path << std::endl;
                return 1;
            }
        }
        std::ostream& out = dump_path == "-" ? std::cout : file;

        DemoParser parser(demo);
        TextDumper dumper(out, parser.state(), options);
        parser.add_listener(dumper);
        parser.run();

        if (dump_path != "-") {
            const auto& state = parser.state();
            std::cout << "Parsed " << demo.header().map_name << " (" << state.players().size() << " players).\n";
            if (!parser.errors().empty()) {
                const auto& first = parser.errors().front();
                std::cout << parser.errors().size() << " errors, see the end of the dump. First: [tick "
                    << first.tick << "] " << first.message << "\n";
            }
            std::cout << "Dumped to: " << dump_path << std::endl;
        }
        return parser.errors().empty() ? 0 : 2;
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to parse demo file: " << e.what() << std::endl;
        return 1;
    }
}
