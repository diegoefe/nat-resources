option(USE_RE "Use libre library" ON)

if(USE_RE)
    # libre
    include_directories(${ibase}/include/re)
    find_library(libre NAMES libre.a PATH ${ibase}/lib)

    if(libre STREQUAL libre-NOTFOUND)
        message(FATAL_ERROR "libre not found!")
    endif()

    list(APPEND res ${libre} OpenSSL::SSL OpenSSL::Crypto dl pthread z)

    add_executable(turn_re turnc.c util.c)
    target_link_libraries(turn_re ${res})
endif()
