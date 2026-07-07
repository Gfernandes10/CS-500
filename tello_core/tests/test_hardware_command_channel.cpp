#include "tello/tello_client.hpp"

#include <iostream>
#include <string>

int main() {
    tello::TelloClient client;

    // Default Tello command endpoint and local command port.
    const tello::ResponseCode init_rc = client.initialize("192.168.10.1", 8889, 8889);
    if (init_rc != tello::ResponseCode::OK) {
        std::cerr << "[test_hardware_command_channel] initialize failed: "
                  << static_cast<int>(init_rc) << std::endl;
        return 1;
    }

    const tello::ResponseCode sdk_rc = client.enterSdkMode();
    if (sdk_rc != tello::ResponseCode::OK) {
        std::cerr << "[test_hardware_command_channel] enterSdkMode failed: "
                  << static_cast<int>(sdk_rc) << std::endl;
        client.shutdown();
        return 1;
    }

    std::string battery;
    const tello::ResponseCode battery_rc = client.getBattery(battery);
    if (battery_rc != tello::ResponseCode::OK || battery.empty()) {
        std::cerr << "[test_hardware_command_channel] battery query failed: rc="
                  << static_cast<int>(battery_rc) << " response=\"" << battery << "\""
                  << std::endl;
        client.shutdown();
        return 1;
    }

    std::cout << "[test_hardware_command_channel] PASS battery=\"" << battery << "\"" << std::endl;
    client.shutdown();
    return 0;
}
