include(${CORE_SOURCE_DIR}/cmake/scripts/common/Macros.cmake)

core_find_versions()
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/addons/xbmc.addon)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/addons/kodi.global.gui)

# configure_file without dependency tracking
# configure_file would register additional file dependencies that interfere
# with the ones from add_custom_command (and the generation would happen twice)
function(generate_versioned_file _SRC _DEST)
  file(READ ${CORE_SOURCE_DIR}/${_SRC} file_content)
  string(CONFIGURE "${file_content}" file_content @ONLY)
  file(WRITE ${CMAKE_BINARY_DIR}/${_DEST} "${file_content}")
endfunction()

# add-on xml's
file(GLOB ADDON_XML_IN_FILE ${CORE_SOURCE_DIR}/addons/*/addon.xml.in)
foreach(loop_var ${ADDON_XML_IN_FILE})
  # prevent 'xbmc.json', becomes done from 'xbmc/interfaces/json-rpc/schema/CMakeLists.txt'.
  if(loop_var MATCHES "xbmc.json")
    continue()
  endif()

  list(GET loop_var 0 xml_name)

  string(REPLACE "/addon.xml.in" "" source_dir ${xml_name})
  string(REPLACE ${CORE_SOURCE_DIR} ${CMAKE_BINARY_DIR} dest_dir ${source_dir})
  file(MAKE_DIRECTORY ${dest_dir})

  # copy everything except addon.xml.in to build folder
  file(COPY "${source_dir}" DESTINATION "${CMAKE_BINARY_DIR}/addons" REGEX ".xml.in" EXCLUDE)

  file(READ "${source_dir}/addon.xml.in" file_content)
  string(CONFIGURE "${file_content}" file_content @ONLY)
  file(WRITE "${dest_dir}/addon.xml" "${file_content}")

  unset(source_dir)
  unset(dest_dir)
  unset(xml_name)
endforeach()

generate_versioned_file(xbmc/CompileInfo.cpp.in ${CORE_BUILD_DIR}/xbmc/CompileInfo.cpp)