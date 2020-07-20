set(ibase $ENV{HOME}/local)
#message("Searching for libs in ${ibase}")

option(USE_PJSIP "Use pjsip library" ON)
option(USE_NICE "Use libnice library" ON)
option(USE_RE "Use libre library" ON)

configure_file(common.sh ${CMAKE_CURRENT_BINARY_DIR} @ONLY)
configure_file(common.sh ${CMAKE_CURRENT_BINARY_DIR}/np1/common.sh @ONLY)

set(OPENSSL_USE_STATIC_LIBS 1)
#set(OPENSSL_ROOT_DIR ${ibase})
find_package(OpenSSL REQUIRED)

if(USE_PJSIP)
    # pjsip
    include_directories(${ibase}/include)
    #if(UNIX) 
        set(sufi "x86_64-unknown-linux-gnu.a")
    #endif()
    find_library(pj pj PATH ${ibase}/lib NAMES libpj-${sufi})
    find_library(pjnat pjnath PATH ${ibase}/lib NAMES libpjnath-${sufi})
    find_library(pju pjlib-util PATH ${ibase}/lib NAMES libpjlib-util-${sufi})

#   message("CMAKE_CXX_LIBRARY_ARCHITECTURE(${CMAKE_CXX_LIBRARY_ARCHITECTURE})")
#   message("CMAKE_LIBRARY_ARCHITECTURE(${CMAKE_LIBRARY_ARCHITECTURE})")
#   message("CMAKE_HOST_SYSTEM_NAME(${CMAKE_HOST_SYSTEM_NAME})")
    
    if(pj STREQUAL pj-NOTFOUND OR pjnat STREQUAL pjnat-NOTFOUND OR pju STREQUAL pju-NOTFOUND)
        message(FATAL_ERROR "pjsip libraries not found!")
    endif()
    
    find_library(uuid uuid)
    if(uuid STREQUAL uuid-NOTFOUND)
        message(FATAL_ERROR "uuid libraries not found!")
    endif()

    list(APPEND pjs ${pjnat} ${pju} ${pj} OpenSSL::SSL OpenSSL::Crypto ${uuid} dl)
    #message("pjs(${pjs})")

    add_executable(ice_pj_ori icedemo.c)
    target_link_libraries(ice_pj_ori ${pjs} pthread m)
    add_executable(tpj tpj.c)
    target_link_libraries(tpj ${pjs} pthread m)

    configure_file(run_pj.sh ${CMAKE_CURRENT_BINARY_DIR} @ONLY)
    configure_file(run_pj_turn.sh ${CMAKE_CURRENT_BINARY_DIR} @ONLY)

    add_subdirectory(np1)
endif()

if(USE_NICE)
    # libnice
    include_directories(${ibase}/include/nice)
    include_directories(/usr/include/glib-2.0)
    include_directories(/usr/lib/x86_64-linux-gnu/glib-2.0/include)
    #find_library(nice nice PATH ${ibase}/lib)
    find_library(nice NAMES libnice.a PATH ${ibase}/lib)

    if(nice STREQUAL nice-NOTFOUND)
        message(FATAL_ERROR "libnice not found!")
    endif()

    list(APPEND nices ${nice} gio-2.0 gobject-2.0 gthread-2.0 pthread glib-2.0 OpenSSL::SSL OpenSSL::Crypto dl pthread)

    #include_directories(${ibase}/include/liveMedia)
    #find_library(liveBUE BasicUsageEnvironment PATH ${ibase}/lib)
    #find_library(liveUE UsageEnvironment PATH ${ibase}/lib)
    #find_library(liveGS groupsock PATH ${ibase}/lib)
    #find_library(live liveMedia PATH ${ibase}/lib)
    #list(APPEND lives ${liveBUE} ${liveGS} ${live})
    ##message("lives(${lives})")

    add_executable(ice_nice_sdp sdp-example.c)
    target_link_libraries(ice_nice_sdp ${nices})
    add_executable(ice_nice_simple simple-example.c)
    target_link_libraries(ice_nice_simple ${nices})
    add_executable(tnice tnice.c)
    target_link_libraries(tnice ${nices})

    configure_file(run_nice.sh ${CMAKE_CURRENT_BINARY_DIR} @ONLY)
endif()

if(USE_RE)
    # libre
    include_directories(${ibase}/include/re)
    find_library(libre NAMES libre.a PATH ${ibase}/lib)

    if(libre STREQUAL libre-NOTFOUND)
        message(FATAL_ERROR "libre not found!")
    endif()

    list(APPEND res ${libre} OpenSSL::SSL OpenSSL::Crypto dl pthread)

    add_executable(turn_re re_turn.c)
    target_link_libraries(turn_re ${res})

    configure_file(run_nice.sh ${CMAKE_CURRENT_BINARY_DIR} @ONLY)
endif()

