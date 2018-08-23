set(ibase $ENV{HOME}/local)

# pjsip
include_directories(${ibase}/include)
find_library(pj pj PATH ${ibase}/lib)
find_library(pjnat pjnath PATH ${ibase}/lib)
find_library(pju pjlib-util PATH ${ibase}/lib)

list(APPEND pjs ${pjnat})
list(APPEND pjs ${pju})
list(APPEND pjs ${pj})
# message("pjs(${pjs})")


add_executable(ice_pj_ori icedemo.c)
target_link_libraries(ice_pj_ori ${pjs} pthread m)
add_executable(tpj tpj.c)
target_link_libraries(tpj ${pjs} pthread m)


# libnice
include_directories(${ibase}/include/nice)
include_directories(/usr/include/glib-2.0)
include_directories(/usr/lib/x86_64-linux-gnu/glib-2.0/include)
find_library(nice nice PATH ${ibase}/lib)
list(APPEND nices ${nice} gio-2.0 gobject-2.0 gthread-2.0 pthread glib-2.0)

add_executable(ice_nice_sdp sdp-example.c)
target_link_libraries(ice_nice_sdp ${nices})
add_executable(ice_nice_simple simple-example.c)
target_link_libraries(ice_nice_simple ${nices})
add_executable(tnice tnice.c)
target_link_libraries(tnice ${nices})

configure_file(run_tnice.sh ${CMAKE_CURRENT_BINARY_DIR} @ONLY)
