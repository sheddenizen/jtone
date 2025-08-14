cmake_minimum_required (VERSION 2.6)
project (jtone)
add_executable(jtone jtone.cpp)

target_compile_options(jtone PUBLIC -Wall)

target_link_libraries(jtone jack boost_program_options)

