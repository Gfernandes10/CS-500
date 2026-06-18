# CompilerOptions.cmake - Additional compiler flags and warnings

# Enable all warnings and treat warnings as errors
if(MSVC)
    # Visual Studio
    add_compile_options(/W4 /WX)
else()
    # GCC / Clang
    add_compile_options(
        -Wall           # All common warnings
        -Wextra         # Extra warnings
        -Wpedantic      # Pedantic warnings
        -Werror         # Treat warnings as errors
        -Wshadow        # Warn about shadowed variables
        -Wunused        # Warn about unused variables
        -Wconversion    # Warn about type conversions
    )
endif()

# Thread support
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)