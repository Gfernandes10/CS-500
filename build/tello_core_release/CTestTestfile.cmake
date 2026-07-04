# CMake generated Testfile for 
# Source directory: /home/gabriel_fernandes/CS 500/tello_core
# Build directory: /home/gabriel_fernandes/CS 500/build/tello_core_release
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(unit_state_parser "/home/gabriel_fernandes/CS 500/build/tello_core_release/test_state_parser")
set_tests_properties(unit_state_parser PROPERTIES  LABELS "unit;offline" _BACKTRACE_TRIPLES "/home/gabriel_fernandes/CS 500/tello_core/CMakeLists.txt;131;add_test;/home/gabriel_fernandes/CS 500/tello_core/CMakeLists.txt;0;")
add_test(unit_metrics_collector "/home/gabriel_fernandes/CS 500/build/tello_core_release/test_metrics_collector")
set_tests_properties(unit_metrics_collector PROPERTIES  LABELS "unit;offline" _BACKTRACE_TRIPLES "/home/gabriel_fernandes/CS 500/tello_core/CMakeLists.txt;138;add_test;/home/gabriel_fernandes/CS 500/tello_core/CMakeLists.txt;0;")
