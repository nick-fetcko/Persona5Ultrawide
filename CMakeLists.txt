cmake_minimum_required(VERSION 3.26.0 FATAL_ERROR)
project(Persona5Ultrawide VERSION 0.1.0 LANGUAGES C CXX)

# Allows usage of configure time content
include(FetchContent)

find_package(minhook CONFIG REQUIRED)

add_subdirectory(ReverseEngineeringUtils)

set(_Persona5Ultrawide_cpp_headers
	dinput8.h
	ShaderBytecode.hpp
)

set(_Persona5Ultrawide_cpp_sources
	dinput8.cpp
	main.cpp
)

add_library(dinput8
	SHARED
	${_Persona5Ultrawide_cpp_headers}
	${_Persona5Ultrawide_cpp_sources}
	exports.def
)

# Prevent windows.h from defining min and max macros
target_compile_definitions(dinput8 PUBLIC NOMINMAX)

target_compile_features(dinput8 PUBLIC cxx_std_17)

target_link_libraries(dinput8 PRIVATE Utils ReverseEngineeringUtils minhook::minhook)

add_custom_command(TARGET dinput8 POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/ReverseEngineeringUtils/third_party/fasm/FASMX64.DLL" "${CMAKE_BINARY_DIR}/${CMAKE_CONFIGURATION_TYPES}/FASMX64.DLL"
)
