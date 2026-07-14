#include "tello/control_panel/control_backend.hpp"

int main(int argc, char *argv[]) {
  return tello::control_panel::runControlPanel(
      argc, argv, tello::control_panel::makeStandaloneBackend());
}
