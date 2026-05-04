cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()

set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)

project(OpenGLDP LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if(MSVC)
    add_compile_options(/utf-8 /EHsc)
    add_compile_definitions(NOMINMAX)
endif()

if(NOT DEFINED VTK_DIR OR VTK_DIR STREQUAL "")
    set(_default_vtk_dir "C:/Users/lenovo/Desktop/bishe/vtk/vtk-install/lib/cmake/vtk-9.5")
    if(EXISTS "${_default_vtk_dir}/vtk-config.cmake")
        set(VTK_DIR "${_default_vtk_dir}" CACHE PATH "" FORCE)
    endif()
endif()

if(EXISTS "C:/Users/lenovo/Desktop/bishe/vtk/vtk-install")
    list(APPEND CMAKE_PREFIX_PATH "C:/Users/lenovo/Desktop/bishe/vtk/vtk-install")
endif()

set(GLAD_ROOT "C:/Users/lenovo/Desktop/bishe/opengl/glad4.6" CACHE PATH "")

if(EXISTS "${GLAD_ROOT}/include/glad/glad.h" AND EXISTS "${GLAD_ROOT}/src/glad.c")
    set(OPENGDP_GLAD_SOURCE "${GLAD_ROOT}/src/glad.c")
    set(OPENGDP_GLAD_INCLUDE "${GLAD_ROOT}/include")
else()
    set(OPENGDP_GLAD_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/glad.c")
    set(OPENGDP_GLAD_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

find_package(OpenGL REQUIRED)
find_package(Qt5 REQUIRED COMPONENTS Widgets)

find_package(VTK CONFIG REQUIRED COMPONENTS
    CommonCore
    CommonDataModel
    CommonExecutionModel
    FiltersCore
    FiltersGeneral
    IOLegacy
    InteractionStyle
    RenderingCore
    RenderingOpenGL2
    RenderingFreeType
)

set(OPENGDP_VTK_LIBS
    VTK::CommonCore
    VTK::CommonDataModel
    VTK::CommonExecutionModel
    VTK::FiltersCore
    VTK::FiltersGeneral
    VTK::IOLegacy
    VTK::InteractionStyle
    VTK::RenderingCore
    VTK::RenderingOpenGL2
    VTK::RenderingFreeType
)

add_library(opengldp_core STATIC
    CAEInterfaceTypes.h
    CAEProcessingFacade.h
    CAEProcessingFacade.cpp
    DataObject.h
    DataObject.cpp
    GLGradientEngine.h
    GLGradientEngine.cpp
    OpenGLManager.h
    gl_context_utils.h
    VTKDataConverter.h
    VTKDataConverter.cpp
    GLFilterEngine.h
    GLFilterEngine.cpp
    ${OPENGDP_GLAD_SOURCE}
    GLFilterEngine.cpp
)

target_include_directories(opengldp_core PUBLIC
    ${OPENGDP_GLAD_INCLUDE}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(opengldp_core PUBLIC
    OpenGL::GL
    ${OPENGDP_VTK_LIBS}
)

add_executable(opengldp_gui
    app/main.cpp
    app/MainWindow.h
    app/MainWindow.cpp
)

target_link_libraries(opengldp_gui PRIVATE
    opengldp_core
    Qt5::Widgets
)

add_executable(opengldp_benchmark
    TestGradient.cpp
)

target_link_libraries(opengldp_benchmark PRIVATE
    opengldp_core
)

add_executable(opengldp_multiscale_test
    TestMultiScale.cpp
)

target_link_libraries(opengldp_multiscale_test PRIVATE
    opengldp_core
)

add_executable(opengldp_field_metrics
    TestFieldMetrics.cpp
)

target_link_libraries(opengldp_field_metrics PRIVATE
    opengldp_core
)

add_custom_command(TARGET opengldp_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Shaders
            $<TARGET_FILE_DIR:opengldp_gui>/Shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Data
            $<TARGET_FILE_DIR:opengldp_gui>/Data
)

add_custom_command(TARGET opengldp_benchmark POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Shaders
            $<TARGET_FILE_DIR:opengldp_benchmark>/Shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Data
            $<TARGET_FILE_DIR:opengldp_benchmark>/Data
)

add_custom_command(TARGET opengldp_multiscale_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Shaders
            $<TARGET_FILE_DIR:opengldp_multiscale_test>/Shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Data
            $<TARGET_FILE_DIR:opengldp_multiscale_test>/Data
)

add_custom_command(TARGET opengldp_field_metrics POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Data
            $<TARGET_FILE_DIR:opengldp_field_metrics>/Data
)


vtk_module_autoinit(
    TARGETS opengldp_gui opengldp_benchmark opengldp_multiscale_test opengldp_field_metrics
    MODULES ${OPENGDP_VTK_LIBS}
)

