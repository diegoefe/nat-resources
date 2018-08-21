set(pjbase $ENV{HOME}/local)
find_library(pj pj PATH ${pjbase}/lib)
find_library(pjnat pjnath PATH ${pjbase}/lib)
find_library(pju pjlib-util PATH ${pjbase}/lib)

include_directories(${pjbase}/include)
add_executable(ice_pj icedemo.c)
target_link_libraries(ice_pj ${pjnat} ${pju} ${pj} pthread m)
message("pjnat(${pjnat})")
message("pj(${pj})")
message("pju(${pju})")

