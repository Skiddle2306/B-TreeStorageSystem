cmake_minimum_required(VERSION 3.20)

project(BTreeStorageSystem LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(BTreeStorageSystem
    
    src/test.cpp
    src/b_plus_tree.cpp
)

target_include_directories(BTreeStorageSystem PRIVATE
    include
)