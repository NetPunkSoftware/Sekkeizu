include(FetchContent)


# Require Git for mongoc patches
find_package(Git REQUIRED)



####################################################################################
# Boost
find_package(Boost ${BOOST_VERSION} REQUIRED)
target_include_directories(sekkeizu SYSTEM PUBLIC ${Boost_INCLUDE_DIRS})
target_link_directories(sekkeizu PUBLIC ${Boost_LIBRARY_DIRS})



####################################################################################
# Tamashii
FetchContent_Declare(
    umi
    GIT_REPOSITORY https://github.com/NetPunkSoftware/Umi
    GIT_TAG        master
)

FetchContent_GetProperties(umi)
if(NOT umi_POPULATED)
    FetchContent_Populate(umi)
    add_subdirectory(${umi_SOURCE_DIR} ${umi_BINARY_DIR})
endif()

target_link_libraries_system(sekkeizu PUBLIC umi_core_lib)



####################################################################################
# Mongo-c-driver
set(ENABLE_SASL             "OFF" CACHE INTERNAL "")
set(ENABLE_SSL              "OFF" CACHE INTERNAL "")
set(ENABLE_TESTS            OFF CACHE INTERNAL "")
set(ENABLE_EXAMPLES         OFF CACHE INTERNAL "")

FetchContent_Declare(
    mongoc
    GIT_REPOSITORY https://github.com/mongodb/mongo-c-driver
    GIT_TAG        5bb9926bfa649d2d271ace15f3a2620e2e2bfc42 # 1.21
)

FetchContent_GetProperties(mongoc)
if(NOT mongoc_POPULATED)
    FetchContent_Populate(mongoc)

    if (WIN32 AND ${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} apply --whitespace=fix ${CMAKE_CURRENT_SOURCE_DIR}/mongoc-win32-clang.patch
	    WORKING_DIRECTORY ${mongoc_SOURCE_DIR}
	    ERROR_QUIET)
    endif()

    add_subdirectory(${mongoc_SOURCE_DIR} ${mongoc_BINARY_DIR})
endif()

target_link_libraries(sekkeizu PUBLIC mongoc_static)



####################################################################################
# cryptopp
set(BUILD_SHARED            OFF CACHE INTERNAL "")
set(BUILD_TESTING           OFF CACHE INTERNAL "")

FetchContent_Declare(
    cryptopp
    GIT_REPOSITORY https://github.com/weidai11/cryptopp
    GIT_TAG        47a6d46db7cbc436d1cc32e64a0f59e613030dec
)

FetchContent_GetProperties(cryptopp)
if(NOT cryptopp_POPULATED)
    FetchContent_Populate(cryptopp)
    
    configure_file (
        "${CMAKE_CURRENT_SOURCE_DIR}/cryptopp.cmake"
        ${cryptopp_SOURCE_DIR}/CMakeLists.txt
        COPYONLY
    )
    
    configure_file (
        "${CMAKE_CURRENT_SOURCE_DIR}/cryptopp.config.cmake.in"
        ${cryptopp_SOURCE_DIR}/cryptopp-config.cmake
        COPYONLY
    )

    if (WIN32 AND ${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} apply --whitespace=fix ${CMAKE_CURRENT_SOURCE_DIR}/cryptopp-win32-clang.patch
            WORKING_DIRECTORY ${cryptopp_SOURCE_DIR}
            ERROR_QUIET)
    endif()

    add_subdirectory(${cryptopp_SOURCE_DIR} ${cryptopp_BINARY_DIR})
endif()

target_link_libraries_system(sekkeizu PUBLIC cryptopp-static)
