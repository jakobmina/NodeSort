# Install rules: library, header, CMake package config and pkg-config file.
include(CMakePackageConfigHelpers)

set(NODESORT_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/nodesort")

install(TARGETS nodesort
  EXPORT nodesortTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT development
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime NAMELINK_COMPONENT development
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)

install(DIRECTORY include/nodesort
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  COMPONENT development)

install(EXPORT nodesortTargets
  NAMESPACE nodesort::
  DESTINATION ${NODESORT_CMAKE_DIR}
  COMPONENT development)

configure_package_config_file(
  cmake/nodesortConfig.cmake.in
  "${PROJECT_BINARY_DIR}/nodesortConfig.cmake"
  INSTALL_DESTINATION ${NODESORT_CMAKE_DIR})

# 0.x minor releases may break the API, so only the same minor is compatible.
if(${PROJECT_VERSION_MAJOR} EQUAL 0)
  set(_ns_compat SameMinorVersion)
else()
  set(_ns_compat SameMajorVersion)
endif()
write_basic_package_version_file(
  "${PROJECT_BINARY_DIR}/nodesortConfigVersion.cmake"
  COMPATIBILITY ${_ns_compat})

install(FILES
  "${PROJECT_BINARY_DIR}/nodesortConfig.cmake"
  "${PROJECT_BINARY_DIR}/nodesortConfigVersion.cmake"
  DESTINATION ${NODESORT_CMAKE_DIR}
  COMPONENT development)

# pkg-config
if(BUILD_SHARED_LIBS)
  set(NODESORT_PC_CFLAGS "")
else()
  set(NODESORT_PC_CFLAGS "-DNODESORT_STATIC")
endif()
if(WIN32)
  set(NODESORT_PC_LIBS_PRIVATE "")
else()
  set(NODESORT_PC_LIBS_PRIVATE "-pthread")
endif()
if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
  set(NODESORT_PC_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
else()
  set(NODESORT_PC_LIBDIR "\${prefix}/${CMAKE_INSTALL_LIBDIR}")
endif()
if(IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}")
  set(NODESORT_PC_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}")
else()
  set(NODESORT_PC_INCLUDEDIR "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
endif()
configure_file(cmake/nodesort.pc.in "${PROJECT_BINARY_DIR}/nodesort.pc" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/nodesort.pc"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
  COMPONENT development)

install(FILES LICENSE README.md CHANGELOG.md
  DESTINATION ${CMAKE_INSTALL_DOCDIR}
  COMPONENT runtime)
