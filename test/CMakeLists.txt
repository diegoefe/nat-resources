set(pjbase $ENV{HOME}/local)
find_library(pj pj PATH ${pjbase}/lib)
find_library(pjnat pjnath PATH ${pjbase}/lib)
find_library(pju pjlib-util PATH ${pjbase}/lib)

list(APPEND pjs ${pjnat})
list(APPEND pjs ${pju})
list(APPEND pjs ${pj})

message("pjs(${pjs})")

include_directories(${pjbase}/include)

add_executable(ice_pj icedemo.c)
target_link_libraries(ice_pj ${pjs} pthread m)

add_executable(ice1 ice1.c)
target_link_libraries(ice1 ${pjs} pthread m)
