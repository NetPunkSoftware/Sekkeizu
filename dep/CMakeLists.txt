include(FetchContent)



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
    add_subdirectory(${mongoc_SOURCE_DIR} ${mongoc_BINARY_DIR})
endif()

target_link_libraries(sekkeizu PUBLIC mongoc_static)



####################################################################################
# Mongo-c-driver -> Paths