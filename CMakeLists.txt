cmake_minimum_required(VERSION 3.20)
project(route LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)

file(GLOB_RECURSE ROUTE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cc)
add_executable(route ${ROUTE_SOURCES})
target_include_directories(route PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(route PRIVATE Drogon::Drogon jwt-cpp::jwt-cpp)

add_custom_command(TARGET route POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/config.json $<TARGET_FILE_DIR:route>/config.json
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/web $<TARGET_FILE_DIR:route>/web)

# ---- Unit tests (GoogleTest) ----
find_package(GTest CONFIG REQUIRED)
enable_testing()
add_executable(route_tests ${CMAKE_SOURCE_DIR}/tests/test_pricing.cc)
target_include_directories(route_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(route_tests PRIVATE GTest::gtest GTest::gtest_main)
add_test(NAME route_tests COMMAND route_tests)
