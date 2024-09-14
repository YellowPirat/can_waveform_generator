cmake_minimum_required(VERSION 3.10)
project(CanGen)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(dbcppp REQUIRED)
find_package(Boost REQUIRED)
find_package(yaml-cpp REQUIRED)

add_subdirectory(extern/)


add_executable(${PROJECT_NAME} main.cpp )

target_link_libraries(${PROJECT_NAME} PUBLIC dbcppp::dbcppp Boost::headers yaml-cpp::yaml-cpp sockcanpp)
#target_link_libraries(${PROJECT_NAME} sockcanpp)