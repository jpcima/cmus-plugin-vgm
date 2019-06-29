cmake_minimum_required(VERSION "3.3")

project(cmus-vgm VERSION "0.0.1" LANGUAGES C CXX)

include(GNUInstallDirs)

add_subdirectory("thirdparty/libvgm" EXCLUDE_FROM_ALL)

add_library(cmus-vgm MODULE "sources/vgm.cc")
target_include_directories(cmus-vgm PRIVATE "thirdparty/cmus")
target_link_libraries(cmus-vgm PRIVATE vgm-player)

set_target_properties(cmus-vgm PROPERTIES
  OUTPUT_NAME "vgm"
  PREFIX ""
  C_VISIBILITY_PRESET "hidden"
  CXX_VISIBILITY_PRESET "hidden"
  CMAKE_VISIBILITY_INLINES_HIDDEN ON
  CXX_STANDARD 11
  CXX_STANDARD_REQUIRED ON)

install(TARGETS cmus-vgm
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmus/ip")
