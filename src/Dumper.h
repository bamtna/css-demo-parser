#pragma once
#include <fstream>
#include <iostream>
#include <iomanip> 

class Demo;

class Dumper {
    const Demo& demo;
    mutable std::ofstream file;

public:
    Dumper(const Demo& demo)
        : demo(demo) {}

    bool open(const std::string& output_file_path) const;
    void dump_header() const;
    void dump_messages() const;
    void close() const;

private:
    const std::string line_break = "=======================================\n";
};

