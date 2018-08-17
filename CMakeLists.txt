cmake_minimum_required(VERSION 2.8)
project(nat-transversal-resources)

if(MINGW OR UNIX)
	# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
   if(MINGW)
      add_definitions(-U__STRICT_ANSI__)
   endif()
endif()

add_subdirectory(nat-punch)
