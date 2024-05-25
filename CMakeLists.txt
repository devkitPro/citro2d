cmake_minimum_required(VERSION 3.13)

project(citro2d
	LANGUAGES C ASM
)

include(GNUInstallDirs)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX "${CTR_ROOT}" CACHE PATH "" FORCE)
endif()

# Define static library target
add_library(citro2d STATIC)
set_target_properties(citro2d PROPERTIES DEBUG_POSTFIX "d")

# Add compiler flags
target_compile_options(citro2d PRIVATE
	-Wall -Werror -DCITRO2D_BUILD
)

# Add include directories
target_include_directories(citro2d PRIVATE include)

target_sources(citro2d PRIVATE
	source/base.c
	source/font.c
	source/spritesheet.c
	source/text.c
)

ctr_add_shader_library(render2d
	source/render2d.v.pica
)

dkp_add_embedded_binary_library(c2d_shaders
	render2d
)

target_link_libraries(citro2d PRIVATE c2d_shaders)

# Install the library
install(
	TARGETS citro2d
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# Install headers
install(
	DIRECTORY ${PROJECT_SOURCE_DIR}/include/
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
	FILES_MATCHING
		PATTERN "*.h"
)
