file(GLOB_RECURSE COMMON_SOURCES "common/*.cc")
add_library(runtime_common SHARED ${COMMON_SOURCES})
target_link_libraries(runtime_common ${Boost_LIBRARIES} flex_utils flex_plan_proto)
install_flex_target(runtime_common)

file(GLOB_RECURSE ADHOC_SOURCES "adhoc/*.cc")
add_library(runtime_adhoc SHARED ${ADHOC_SOURCES})
target_link_libraries(runtime_adhoc runtime_common)
install_flex_target(runtime_adhoc)

file(GLOB_RECURSE COMMON_SOURCES "common/*.cc")
add_library(runtime_common_static STATIC ${COMMON_SOURCES})
target_link_libraries(runtime_common_static ${Boost_LIBRARIES} flex_utils flex_plan_proto)
install_flex_target(runtime_common_static)

file(GLOB_RECURSE ADHOC_SOURCES "adhoc/*.cc")
add_library(runtime_adhoc_static STATIC ${ADHOC_SOURCES})
target_link_libraries(runtime_adhoc_static runtime_common_static)
install_flex_target(runtime_adhoc_static)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DESTINATION include/flex/engines/graph_db
        FILES_MATCHING
        PATTERN "*.h"
)
# 收集所有的.h文件，包括子目录
file(GLOB_RECURSE HEADER_FILES "${CMAKE_CURRENT_SOURCE_DIR}/graph_db/*.h")

# 安装文件，保留目录结构
foreach(header_file ${HEADER_FILES})
    # 获取文件的相对路径
    get_filename_component(relative_path ${header_file} DIRECTORY)
    # 获取 graph_db 目录的相对路径
    string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}/graph_db" "" include_path ${relative_path})
    # 安装头文件，保留结构
    install(FILES ${header_file}
            DESTINATION include/flex/engines/graph_db${include_path})
endforeach()