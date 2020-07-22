cmake_minimum_required(VERSION 2.8)
project(nat-transversal-resources)

if(MINGW OR UNIX)
	# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
   if(MINGW)
      add_definitions(-U__STRICT_ANSI__)
   endif()
endif()

add_subdirectory(nat-punch)
set(ibase $ENV{HOME}/local)
#message("Searching for libs in ${ibase}")

option(USE_PJSIP "Use pjsip library" ON)
option(USE_NICE "Use libnice library" ON)
option(USE_RE "Use libre library" ON)

configure_file(common.sh ${PROJECT_BINARY_DIR}/test @ONLY)
configure_file(common.sh ${PROJECT_BINARY_DIR}/test/np1/common.sh @ONLY)

set(OPENSSL_USE_STATIC_LIBS 1)
#set(OPENSSL_ROOT_DIR ${ibase})
find_package(OpenSSL REQUIRED)

add_subdirectory(test)
add_subdirectory(re)
