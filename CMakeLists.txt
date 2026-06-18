cmake_minimum_required(VERSION 3.16)

project(network_chaos_harness
        VERSION 1.0.0
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BUILD_VISUALIZER "Build the ImGui authoring tool (needs GLFW + OpenGL)" OFF)
option(NCH_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
include(CTest)

add_library(nch_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(nch_warnings INTERFACE -Wall -Wextra)
  if(NCH_WARNINGS_AS_ERRORS)
    target_compile_options(nch_warnings INTERFACE -Werror)
  endif()
endif()

# ---------------------------------------------------------------------------
# Dependencies
#
# Each external dependency prefers a system package and falls back to a vendored
# copy under third_party/ so the project also builds on an air-gapped host.
# ---------------------------------------------------------------------------

# nlohmann/json (header only).
find_package(nlohmann_json QUIET)
add_library(nch_json INTERFACE)
if(nlohmann_json_FOUND)
  target_link_libraries(nch_json INTERFACE nlohmann_json::nlohmann_json)
  message(STATUS "nlohmann/json: using system package")
else()
  target_include_directories(nch_json INTERFACE
                             ${CMAKE_CURRENT_SOURCE_DIR}/third_party/json)
  message(STATUS "nlohmann/json: using vendored header (third_party/json)")
endif()

# libpcap (optional). The data-plane telemetry component has a portable
# bound-socket fallback when libpcap is unavailable.
find_path(PCAP_INCLUDE_DIR pcap.h)
find_library(PCAP_LIBRARY pcap)
add_library(nch_pcap INTERFACE)
if(PCAP_INCLUDE_DIR AND PCAP_LIBRARY)
  target_include_directories(nch_pcap INTERFACE ${PCAP_INCLUDE_DIR})
  target_link_libraries(nch_pcap INTERFACE ${PCAP_LIBRARY})
  target_compile_definitions(nch_pcap INTERFACE HAVE_LIBPCAP)
  message(STATUS "libpcap: found, building pcap capture backend")
else()
  message(STATUS "libpcap: not found, using bound-socket capture fallback")
endif()

# ---------------------------------------------------------------------------
# Core libraries
# ---------------------------------------------------------------------------

# Shared data contract (Node/Link/Topology, serialization, address plan).
add_library(nch_common
  common/topology.cpp
  common/address_plan.cpp)
target_include_directories(nch_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nch_common PUBLIC nch_json PRIVATE nch_warnings)

# Tier 4 chaos engine building blocks (no orchestrator: keeps the layering acyclic).
add_library(nch_chaos
  chaos_engine/topology_parser.cpp
  chaos_engine/reference_topology_solver.cpp
  chaos_engine/system_execution_engine.cpp
  chaos_engine/data_plane_telemetry.cpp
  chaos_engine/control_plane_asserter.cpp
  chaos_engine/stabilization_gate.cpp
  chaos_engine/protocol_state.cpp
  chaos_engine/convergence.cpp)
target_link_libraries(nch_chaos
  PUBLIC nch_common nch_json
  PRIVATE nch_pcap nch_warnings)
find_package(Threads REQUIRED)
target_link_libraries(nch_chaos PUBLIC Threads::Threads)

# Tier 3 LinkShaper (depends on the execution engine for tc invocation).
add_library(nch_link_shaper substrate/link_shaper.cpp)
target_link_libraries(nch_link_shaper PUBLIC nch_chaos nch_common PRIVATE nch_warnings)

# Top-level orchestrator: ties the engine and shaper together. Higher layer than
# both, so there is no dependency cycle.
add_library(nch_orchestrator chaos_engine/orchestrator.cpp)
target_link_libraries(nch_orchestrator
  PUBLIC nch_chaos nch_link_shaper nch_common
  PRIVATE nch_pcap nch_warnings)

# Tier 1 GUI-independent core (graph model + git integration).
add_library(nch_visualizer_core
  visualizer/graph_state.cpp
  visualizer/git_integrator.cpp)
target_link_libraries(nch_visualizer_core PUBLIC nch_common PRIVATE nch_warnings)

# ---------------------------------------------------------------------------
# Executables
# ---------------------------------------------------------------------------

add_executable(chaos_engine chaos_engine/main.cpp)
target_link_libraries(chaos_engine PRIVATE nch_orchestrator nch_warnings)

add_executable(link_shaper_cli substrate/link_shaper_cli.cpp)
target_link_libraries(link_shaper_cli PRIVATE nch_link_shaper nch_warnings)

# ---------------------------------------------------------------------------
# Tier 1 visualizer (optional: requires GLFW, OpenGL and ImGui)
# ---------------------------------------------------------------------------
if(BUILD_VISUALIZER)
  find_package(OpenGL QUIET)
  find_package(glfw3 QUIET)
  set(IMGUI_DIR "" CACHE PATH "Path to a Dear ImGui checkout (with backends/)")

  if(OpenGL_FOUND AND glfw3_FOUND AND IMGUI_DIR AND EXISTS "${IMGUI_DIR}/imgui.cpp")
    add_executable(visualizer
      visualizer/main.cpp
      visualizer/canvas_render.cpp
      ${IMGUI_DIR}/imgui.cpp
      ${IMGUI_DIR}/imgui_draw.cpp
      ${IMGUI_DIR}/imgui_tables.cpp
      ${IMGUI_DIR}/imgui_widgets.cpp
      ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
      ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(visualizer PRIVATE
      ${IMGUI_DIR}
      ${IMGUI_DIR}/backends)
    target_link_libraries(visualizer PRIVATE
      nch_visualizer_core nch_common glfw OpenGL::GL)
  else()
    message(WARNING
      "BUILD_VISUALIZER is ON but OpenGL/GLFW/ImGui were not all found. "
      "Set -DIMGUI_DIR=<path-to-imgui> and install GLFW/OpenGL to build it. "
      "Skipping the visualizer target.")
  endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
  find_package(GTest QUIET)
  add_library(nch_gtest INTERFACE)
  if(GTest_FOUND)
    target_link_libraries(nch_gtest INTERFACE GTest::gtest GTest::gtest_main)
    message(STATUS "GoogleTest: using system package")
  else()
    add_library(nch_gtest_vendored STATIC third_party/gtest/gtest_main.cpp)
    target_include_directories(nch_gtest_vendored PUBLIC
                               ${CMAKE_CURRENT_SOURCE_DIR}/third_party/gtest)
    target_link_libraries(nch_gtest INTERFACE nch_gtest_vendored)
    message(STATUS "GoogleTest: using vendored harness (third_party/gtest)")
  endif()

  add_executable(unit_tests
    tests/unit/test_json.cpp
    tests/unit/test_topology_parser.cpp
    tests/unit/test_reference_solver.cpp
    tests/unit/test_link_shaper.cpp
    tests/unit/test_address_plan.cpp
    tests/unit/test_graph_state.cpp
    tests/unit/test_protocol_state.cpp)
  target_link_libraries(unit_tests PRIVATE
    nch_chaos nch_link_shaper nch_visualizer_core nch_common nch_gtest)
  add_test(NAME unit_tests COMMAND unit_tests)

  add_executable(integration_tests tests/integration/test_matrix.cpp)
  target_compile_definitions(integration_tests PRIVATE
    NCH_TOPOLOGY_PATH="${CMAKE_CURRENT_SOURCE_DIR}/topology.json")
  target_link_libraries(integration_tests PRIVATE nch_orchestrator nch_gtest)
  add_test(NAME integration_tests COMMAND integration_tests)
endif()
