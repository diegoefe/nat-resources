add_executable(nat-client nat-client.cpp nat-reg.cpp)
add_executable(nat-server nat-server.cpp nat-reg.cpp)
if(NOT UNIX)
	add_definitions(-DWIN32)
   list(APPEND libs ws2_32)
endif()
target_link_libraries(nat-client ${libs})
target_link_libraries(nat-server ${libs})

