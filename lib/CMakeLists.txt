set(DLISIO_LIB_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR} CACHE PATH "")

add_library(dlisio src/dlis/dlisio.cpp
                   src/dlis/types.cpp
                   src/dlis/records.cpp
                   src/dlis/io.cpp
                   src/lis/protocol.cpp
                   src/lis/types.cpp
                   src/lis/pack.cpp
                   src/lis/io.cpp
                   src/file.cpp
                   src/tapemark.cpp
)
target_include_directories(dlisio
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<INSTALL_INTERFACE:include>
    PRIVATE
        # Work-around as described in [1]. Even privately linked interface
        # libraries must be exported, because cmake does not check if anything
        # other than include-paths are set. The build-local export file is
        # given to the python build script, so endianness must also be
        # exported.
        #
        # in time, this hack should go away, either by promotoing endianness to
        # a library in its own right, or hard-code the include path
        # (effectively what is done now)
        # [1] https://gitlab.kitware.com/cmake/cmake/issues/18049
        $<TARGET_PROPERTY:endianness::endianness,INTERFACE_INCLUDE_DIRECTORIES>
)
target_compile_options(dlisio
    BEFORE
    PRIVATE
        $<$<CONFIG:Debug>:${warnings-c++}>
        $<$<CXX_COMPILER_ID:MSVC>:/EHsc>
)

target_compile_definitions(dlisio
    PRIVATE
    $<$<BOOL:${BIG_ENDIAN}>:HOST_BIG_ENDIAN>
    $<$<NOT:$<BOOL:${BIG_ENDIAN}>>:HOST_LITTLE_ENDIAN>
)
target_link_libraries(dlisio
    PUBLIC mpark_variant
           lfp::lfp

    PRIVATE fmt::fmt-header-only
)
install(TARGETS dlisio
        EXPORT dlisio
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT dlisio DESTINATION share/dlisio/cmake FILE dlisio-config.cmake)

export(
    TARGETS
        dlisio
        endianness
    FILE
        dlisio-config.cmake
)

if (BUILD_SHARED_LIBS)
    target_compile_definitions(dlisio
        PRIVATE   DLISIO_EXPORT
        INTERFACE DLISIO_SHARED
    )
endif ()

if(NOT BUILD_TESTING)
    return()
endif()

add_executable(test-dlis test/testsuite.cpp
                         test/dlis/protocol.cpp
                         test/dlis/types.cpp
                         test/dlis/sul.cpp
                         test/dlis/pack.cpp
)
target_compile_options(test-dlis
    BEFORE
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/EHsc>
)
target_link_libraries(test-dlis dlisio catch2)
add_test(NAME dlis-core COMMAND test-dlis)

add_executable(test-lis test/testsuite.cpp
                        test/lis/types.cpp
                        test/lis/protocol.cpp
                        test/lis/io.cpp
                        test/lis/pack.cpp
)
target_compile_options(test-lis
    BEFORE
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/EHsc>
)
target_link_libraries(test-lis dlisio catch2)
add_test(NAME lis-core COMMAND test-lis)
