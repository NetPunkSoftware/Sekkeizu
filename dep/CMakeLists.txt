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

target_link_libraries_system(sekkeizu PUBLIC umi)
