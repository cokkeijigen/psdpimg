cmake_minimum_required(VERSION 3.20.0)
project(psdpimg VERSION 1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	set(IS_CLANG_COMPILER 1)
	add_compile_options(
		"-Wno-braced-scalar-init"
		"-fasm-blocks"
	)
	add_link_options("-Wl")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    add_compile_options(
        "/source-charset:utf-8"
        "$<$<CONFIG:Release>:/GF>"
        "$<$<CONFIG:Release>:/Oy>"
        "$<$<CONFIG:Release>:/GL>"
        "$<$<CONFIG:Release>:/GS->"
    )
endif()

string(TIMESTAMP PROJECT_BUILD_DATE "%Y%m%d")
add_definitions("-DPROJECT_BUILD_DATE=\"${PROJECT_BUILD_DATE}\"")
add_definitions("-DPROJECT_NAME=\"${PROJECT_NAME}\"")
add_definitions("-DPROJECT_VERSION=\"${PROJECT_VERSION}\"")

add_executable(psdpimg
    "src/main.cpp"
    "src/common/byte_reader.cpp"
    "src/common/byte_writer.cpp"
    "src/common/image.cpp"
    "src/common/unicode.cpp"
    "src/psb/psb_value.cpp"
    "src/psb/psb_reader.cpp"
    "src/psb/psb_writer.cpp"
    "src/tlg/tlg5.cpp"
    "src/psd/psd_rle.cpp"
    "src/psd/psd_metadata.cpp"
    "src/psd/psd_reader.cpp"
    "src/psd/psd_writer.cpp"
    "src/psd/psd_origin.cpp"
    "src/pimg/pimg_convert.cpp"
)

target_include_directories(psdpimg PUBLIC
    "src/"
)
