#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "tello_core::tello_core" for configuration "Release"
set_property(TARGET tello_core::tello_core APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(tello_core::tello_core PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libtello_core.a"
  )

list(APPEND _cmake_import_check_targets tello_core::tello_core )
list(APPEND _cmake_import_check_files_for_tello_core::tello_core "${_IMPORT_PREFIX}/lib/libtello_core.a" )

# Import target "tello_core::tello_cli" for configuration "Release"
set_property(TARGET tello_core::tello_cli APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(tello_core::tello_cli PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/tello_cli"
  )

list(APPEND _cmake_import_check_targets tello_core::tello_cli )
list(APPEND _cmake_import_check_files_for_tello_core::tello_cli "${_IMPORT_PREFIX}/bin/tello_cli" )

# Import target "tello_core::tello_control_panel" for configuration "Release"
set_property(TARGET tello_core::tello_control_panel APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(tello_core::tello_control_panel PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/tello_control_panel"
  )

list(APPEND _cmake_import_check_targets tello_core::tello_control_panel )
list(APPEND _cmake_import_check_files_for_tello_core::tello_control_panel "${_IMPORT_PREFIX}/bin/tello_control_panel" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
