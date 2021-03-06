set(target random-network-generator)

set(
        SOURCES
        RNGOptions.cpp
        evoRateLaw.cpp
        RNG.cpp
        ../EvoString.cpp
        NetworkComponent.cpp
)

set(
        HEADERS
        RNGOptions.h
        RoleTypes.h
        evoRateLaw.h
        RNG.h
        ../logger.h
        StoicCounter.h
        ../TypeDefs.h
        NetworkComponent.h
        ../EvoString.h
)

add_library(${target}-static STATIC ${SOURCES} ${HEADERS})

target_link_libraries(
        ${target}-static
        PUBLIC
        roadrunner-static::roadrunner-static
        ${MPI_LIBRARY}
)

message(STATUS "NUMCPP_INCLUDE_DIR ${NUMCPP_INCLUDE_DIR}")

target_include_directories(
        ${target}-static PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        $<BUILD_INTERFACE:${RANDOM_NETWORK_GENERATOR_SOURCE_DIR}>
        $<BUILD_INTERFACE:${NUMCPP_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${Boost_INCLUDE_DIRS}>
        $<BUILD_INTERFACE:${EVOGEN_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<BUILD_INTERFACE:${MPI_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
#add_sanitizers(${target}-static)
add_sanitize_address(${target}-static)

target_compile_definitions(
        ${target}-static PUBLIC EVOGEN_STATIC_DEFINE LIBSBML_STATIC
)


add_library(${target} SHARED ${SOURCES} ${HEADERS})
target_link_libraries(${target} PUBLIC $<TARGET_PROPERTY:${target}-static,LINK_LIBRARIES>)
target_include_directories(
        ${target} PUBLIC
        $<BUILD_INTERFACE:$<TARGET_PROPERTY:${target}-static,INCLUDE_DIRECTORIES>>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

generate_export_header(
        ${target}
        BASE_NAME RANDOM_NETWORK_GENERATOR
        EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/random_network_generator_export.h"
)

#add_library(${target}-C-API SHARED RandomNetworkGeneratorCAPI.cpp)
#target_link_libraries(${target}-C-API PUBLIC ${target}-static)
#
#set_target_properties(${target}-C-API PROPERTIES PREFIX "")
#add_dependencies(${target}-C-API ${target}-static)
#
#generate_export_header(
#        ${target}-C-API
#        BASE_NAME RANDOM_NETWORK_GENERATOR_C_API
#        EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/random_network_generator_c_api_export.h"
#)
#

pybind11_add_module(evogen MODULE evogen.cpp)
target_link_libraries(evogen PRIVATE roadrunner-static::roadrunner-static random-network-generator-static)
target_include_directories(evogen PRIVATE "$<TARGET_PROPERTY:${target},INCLUDE_DIRECTORIES>")














