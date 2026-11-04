# Deadstation's frontend: runs the core (pcsx_core) on the Brick, drawing through Vulkan to fb0.
if(NOT DEADSTATION_VULKAN_INCLUDE OR NOT EXISTS "${DEADSTATION_VULKAN_INCLUDE}/vulkan/vulkan.hpp")
	message(FATAL_ERROR "DEADSTATION_VULKAN_INCLUDE: the folder with the Vulkan headers (vulkan/vulkan.hpp)")
endif()
find_package(SDL2 REQUIRED)

# the shaders, compiled to SPIR-V by the host's glslangValidator, into headers
find_program(DEADSTATION_GLSLANG glslangValidator NO_CMAKE_FIND_ROOT_PATH REQUIRED)
set(_spirv)
foreach(_shader frame.vert frame.frag frame_exact.frag)
	string(REPLACE "." "_" _name ${_shader})
	set(_out ${CMAKE_CURRENT_BINARY_DIR}/shaders/${_name}.h)
	add_custom_command(OUTPUT ${_out}
			COMMAND ${DEADSTATION_GLSLANG} -V --quiet --vn ${_name} -o ${_out} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${_shader}
			DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${_shader}
			COMMENT "SPIR-V ${_shader}")
	list(APPEND _spirv ${_out})
endforeach()

add_executable(deadstation main.cpp controls.cpp controls.h link.cpp link.h link_core.h link_bluetooth.cpp link_usb.cpp log.h
		menu.cpp menu.h netplay.cpp netplay.h osd.cpp osd.h panel.cpp panel.h settings.cpp settings.h types.h
		states.cpp states.h video.cpp video.h vk.h ${_spirv})
target_include_directories(deadstation PRIVATE ${DEADSTATION_VULKAN_INCLUDE} ${CMAKE_CURRENT_BINARY_DIR}/shaders)
target_compile_definitions(deadstation PRIVATE DEADSTATION_VERSION="${DEADSTATION_VERSION}" DEADSTATION_APP_VERSION="${PROJECT_VERSION}")
target_compile_options(deadstation PRIVATE -O2 -Wall)
find_package(ZLIB REQUIRED)
target_link_libraries(deadstation PRIVATE pcsx_core deadui SDL2::SDL2 ZLIB::ZLIB)
