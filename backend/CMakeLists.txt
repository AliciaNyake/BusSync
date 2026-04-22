cmake_minimum_required(VERSION 3.16)

project(bussync_backend_c C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(bussync_server
  src/main.c
  src/http.c
  src/database.c
  src/mail.c
  third_party/sqlite/sqlite3.c
)

target_include_directories(bussync_server PRIVATE
  include
  third_party/sqlite
)

if (WIN32)
  target_link_libraries(bussync_server ws2_32)
endif()