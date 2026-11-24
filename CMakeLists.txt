cmake_minimum_required(VERSION 3.16)
project(UnnecessaryBoomkinReviewer CXX)
if(WIN32)
  enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CORE
    src/core/spells.cpp src/core/wcl.cpp src/core/analysis.cpp
    src/core/i18n.cpp src/core/report.cpp src/core/demo.cpp)

add_library(ubr_core STATIC ${CORE})
if(MSVC)
  target_compile_options(ubr_core PUBLIC /utf-8 /W4)
endif()

add_executable(ubr_tests tests/test_main.cpp)
target_link_libraries(ubr_tests ubr_core)

if(WIN32)
  if(MSVC)
    # static CRT: one exe, no VC++ redistributable needed
    set_property(TARGET ubr_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_executable(UnnecessaryBoomkinReviewer WIN32
      src/win/main.cpp src/win/winutil.cpp src/win/http_win.cpp src/win/graph.cpp res/app.rc)
  target_compile_definitions(UnnecessaryBoomkinReviewer PRIVATE UNICODE _UNICODE)
  target_link_libraries(UnnecessaryBoomkinReviewer ubr_core winhttp comctl32 shell32 advapi32 gdi32 user32)
  if(MSVC)
    set_property(TARGET UnnecessaryBoomkinReviewer PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_options(UnnecessaryBoomkinReviewer PRIVATE /ENTRY:wWinMainCRTStartup /MANIFEST:NO)
  else()
    target_link_options(UnnecessaryBoomkinReviewer PRIVATE -municode -static)
  endif()
endif()
