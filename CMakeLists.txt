cmake_minimum_required(VERSION 3.25)

project(Imprint LANGUAGES CXX)

find_package(yaml-cpp CONFIG REQUIRED)

add_executable(imprint
	Imprint/Source/Executable.cpp
	Imprint/Source/Executable.h
	Imprint/Source/Imprint.cpp
	Imprint/Source/Manifest.cpp
	Imprint/Source/Manifest.h
	Imprint/Source/Version.cpp
	Imprint/Source/Version.h
)

target_compile_features(imprint PRIVATE cxx_std_20)
target_compile_definitions(imprint PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(imprint PRIVATE /W3 /sdl /permissive- /utf-8)
target_link_libraries(imprint PRIVATE yaml-cpp::yaml-cpp xmllite shlwapi)

set_target_properties(imprint PROPERTIES
	MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
	INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)
