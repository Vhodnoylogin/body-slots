cmake_minimum_required(VERSION 3.24)
project(VRInteractionFrameworkTest VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(RC4_NATIVE "" CACHE PATH "Previously built RC4 native tree (Release libraries)")
set(HIGGS_ROOT "" CACHE PATH "HIGGS public contract checkout")
set(PLANCK_ROOT "" CACHE PATH "PLANCK public contract checkout")
set(JSON_INCLUDE "" CACHE PATH "Directory containing nlohmann/json.hpp")
if(NOT EXISTS "${RC4_NATIVE}/CMakeCache.txt")
  message(FATAL_ERROR "RC4_NATIVE must point to the completed native build")
endif()
file(STRINGS "${RC4_NATIVE}/CMakeCache.txt" dependency_paths REGEX "^FETCHCONTENT_SOURCE_DIR_(COMMONLIBSSE|FMT|SPDLOG|RAPIDCSV):PATH=")
foreach(line IN LISTS dependency_paths)
  if(line MATCHES "^FETCHCONTENT_SOURCE_DIR_([A-Z]+):PATH=(.*)$")
    set(DEP_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
  endif()
endforeach()
get_filename_component(FRAMEWORK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/contracts")
foreach(header IN ITEMS higgsinterface001.h higgsabitypes.h higgshandapi.h higgsinput.h)
  configure_file("${HIGGS_ROOT}/include/${header}" "${CMAKE_CURRENT_BINARY_DIR}/contracts/${header}" COPYONLY)
endforeach()
configure_file("${PLANCK_ROOT}/include/planck_synergy.h" "${CMAKE_CURRENT_BINARY_DIR}/contracts/planck_synergy.h" COPYONLY)
add_library(test_VRInteractionFramework SHARED fixture.cpp DevBenchAPI.cpp)
target_include_directories(test_VRInteractionFramework PRIVATE
  "${FRAMEWORK_ROOT}/src/compat" "${FRAMEWORK_ROOT}/include"
  "${CMAKE_CURRENT_BINARY_DIR}/contracts" "${DEP_COMMONLIBSSE}/include"
  "${DEP_FMT}/include" "${DEP_SPDLOG}/include" "${DEP_RAPIDCSV}/src" "${JSON_INCLUDE}")
target_compile_definitions(test_VRInteractionFramework PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
  ENABLE_SKYRIM_SE=1 ENABLE_SKYRIM_AE=1 ENABLE_SKYRIM_VR=1 HAS_SKYRIM_MULTI_TARGETING=1
  SPDLOG_COMPILED_LIB SPDLOG_FMT_EXTERNAL _CRT_SECURE_NO_WARNINGS)
target_compile_options(test_VRInteractionFramework PRIVATE /W4 /permissive- /EHsc /utf-8 /wd4200 /wd4201 /wd4324)
target_link_libraries(test_VRInteractionFramework PRIVATE
  "${RC4_NATIVE}/_deps/commonlibsse-build/Release/CommonLibSSE.lib"
  "${RC4_NATIVE}/_deps/fmt-build/Release/fmt.lib"
  "${RC4_NATIVE}/_deps/spdlog-build/Release/spdlog.lib"
  version bcrypt dbghelp)
