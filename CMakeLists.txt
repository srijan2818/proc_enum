cmake_minimum_required(VERSION 3.15)
project(ProcessMonitor)
set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  GIT_TAG v5.0.0
)
FetchContent_MakeAvailable(ftxui)

add_executable(process_monitor src/source.cpp)
target_link_libraries(process_monitor
  PRIVATE ftxui::screen
  PRIVATE ftxui::dom
  PRIVATE ftxui::component
)