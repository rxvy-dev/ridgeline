cmake_minimum_required(VERSION 3.16)
project(ridgeline VERSION 3.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
  add_library(SDL3::SDL3 ALIAS PkgConfig::SDL3)
endif()

add_executable(ridgeline
  src/main.cpp
  src/gl.cpp
  src/render.cpp
  src/textures.cpp
  src/world.cpp
  src/cars.cpp
  src/carimport.cpp src/gps.cpp src/features.cpp src/effects.cpp src/special.cpp src/custom.cpp src/estates.cpp src/homes.cpp src/eventlab.cpp src/gates.cpp src/erosion.cpp src/festival.cpp src/assets.cpp src/props.cpp src/urban.cpp
  src/inflate.cpp
  src/vehicle.cpp
  src/race.cpp
  src/hud.cpp
  src/audio.cpp
  src/game.cpp
)
target_link_libraries(ridgeline PRIVATE SDL3::SDL3)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(ridgeline PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

# Make a locally bundled SDL3 (installed next to the binary by install.sh) resolvable.
set_target_properties(ridgeline PROPERTIES INSTALL_RPATH "$ORIGIN/../lib/ridgeline")

include(GNUInstallDirs)
install(TARGETS ridgeline RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/ridgeline.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/ridgeline.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
foreach(sz 16 24 32 48 64 128 256 512)
  install(FILES packaging/icons/ridgeline-${sz}.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${sz}x${sz}/apps RENAME ridgeline.png)
endforeach()
install(FILES packaging/icons/ridgeline-256.png DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps RENAME ridgeline.png)
