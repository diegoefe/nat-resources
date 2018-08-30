set(PRG cam)
configure_file(run_np1.sh ${CMAKE_CURRENT_BINARY_DIR}/run_cam.sh @ONLY)

add_executable(cam cam.c pjwrap.c)
target_link_libraries(cam ${pjs} pthread m)

