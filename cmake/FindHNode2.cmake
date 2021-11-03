# Try to find libhnode2 library.
# It can be used as :
#  
# find_package(HNode2 REQUIRED)
# target_link_libraries(program HNode2::common)
#

set(HNode2_FOUND OFF)
set(HNode2_COMMON_FOUND OFF)

# avahi-common & avahi-client
find_library(HNode2_COMMON_LIBRARY NAMES hnode2)
find_path(HNode2_COMMON_INCLUDE_DIRS hnode2/HNodeConfig.h)

if (HNode2_COMMON_LIBRARY AND HNode2_COMMON_INCLUDE_DIRS)
  set(HNode2_FOUND ON)
  set(HNode2_COMMON_FOUND ON)

  if (NOT TARGET "HNode2::common")

    add_library("HNode2::common" UNKNOWN IMPORTED)
    set_target_properties("HNode2::common"
      PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${HNode2_COMMON_INCLUDE_DIRS}"
      IMPORTED_LOCATION "${HNode2_COMMON_LIBRARY}"
    )

    get_filename_component(HNode2_LIB_DIR "${HNode2_COMMON_LIBRARY}" DIRECTORY CACHE)

  endif()

endif()

set(_HNode2_MISSING_COMPONENTS "")
foreach(COMPONENT ${HNode2_FIND_COMPONENTS})
  string(TOUPPER ${COMPONENT} COMPONENT)

  if(NOT I2C_${COMPONENT}_FOUND)
    string(TOLOWER ${COMPONENT} COMPONENT)
    list(APPEND _HNode2_MISSING_COMPONENTS ${COMPONENT})
  endif()
endforeach()

if (_HNode2_MISSING_COMPONENTS)
  set(HNode2_FOUND OFF)

  if (HNode2_FIND_REQUIRED)
    message(SEND_ERROR "Unable to find the requested HNode2 libraries.\n")
  else()
    message(STATUS "Unable to find the requested but not required HNode2 libraries.\n")
  endif()

endif()


if(HNode2_FOUND)
  set(HNode2_LIBRARIES ${HNode2_COMMON_LIBRARY})
  set(HNode2_INCLUDE_DIRS ${HNode2_COMMON_INCLUDE_DIRS})
  
  message(STATUS "Found HNode2: ${HNode2_LIBRARIES}")
endif()
