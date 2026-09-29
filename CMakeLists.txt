cmake_minimum_required(VERSION 3.20)

project(BTreeStorageSystem LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(BTreeStorageSystem
    src/test.cpp
    src/b_plus_tree.cpp
    src/page_manager.cpp
)
add_executable(test1
    
    src/b_plus_tree.cpp
    src/page_manager.cpp
    src/test1.cpp
)

add_executable(PageManagerTest
    src/test_page_manager.cpp
    
    src/page_manager.cpp
)

target_include_directories(BTreeStorageSystem PRIVATE
    include
    src/include)

target_include_directories(test1 PRIVATE
    include
    src/include)

target_include_directories(PageManagerTest PRIVATE
    include
    src/include)