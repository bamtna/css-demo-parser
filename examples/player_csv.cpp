// Writes every player's position, angles and velocity per tick as CSV.
#include "cssdemo/cssdemo.h"
#include <iostream>

using namespace cssdemo;

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <demo_file_path>\n";
        return 1;
    }

    try {
        DemoFile demo(argv[1]);
        DemoParser parser(demo);
        PlayerTracker tracker;
        parser.add_listener(tracker);
        parser.run();

        std::cerr << demo.header().map_name << ", " << demo_kind_name(demo_kind(parser.state())) << " demo, "
            << parser.errors().size() << " errors\n";

        std::cout << "user_id,name,tick,x,y,z,pitch,yaw,vx,vy,vz,velocity_estimated,flags,alive,buttons\n";
        for (const auto& [user_id, samples] : tracker.samples()) {
            for (const auto& s : samples) {
                const auto& p = s.player;
                std::cout << user_id << ",\"" << p.info.name << "\"," << s.tick << ","
                    << p.origin.x << "," << p.origin.y << "," << p.origin.z << ","
                    << p.eye_angles.x << "," << p.eye_angles.y << ","
                    << s.velocity.x << "," << s.velocity.y << "," << s.velocity.z << ","
                    << s.velocity_estimated << "," << p.flags << "," << p.alive() << ","
                    << (s.user_cmd ? s.user_cmd->buttons : 0) << "\n";
            }
        }
        return parser.errors().empty() ? 0 : 2;
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to parse demo file: " << e.what() << std::endl;
        return 1;
    }
}
