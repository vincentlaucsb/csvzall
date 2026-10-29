set(CSVZALL_INDICATORS_TARGET "")
if(NOT CSVZALL_OBSIDIAN_BUILD)
  # indicators (header-only terminal progress bars/spinners)
  set(INDICATORS_INSTALL OFF CACHE BOOL "Disable indicators install target" FORCE)
  set(INDICATORS_PACKAGE OFF CACHE BOOL "Disable indicators package target" FORCE)
  set(INDICATORS_BUILD_TESTS OFF CACHE BOOL "Disable indicators tests" FORCE)
  set(INDICATORS_SAMPLES OFF CACHE BOOL "Disable indicators samples" FORCE)
  set(INDICATORS_DEMO OFF CACHE BOOL "Disable indicators demo" FORCE)
  FetchContent_Declare(
    indicators
    GIT_REPOSITORY https://github.com/p-ranav/indicators.git
    GIT_TAG v2.3
    GIT_SHALLOW TRUE
    PATCH_COMMAND
      ${CMAKE_COMMAND}
        -D "INDICATORS_SOURCE_DIR=<SOURCE_DIR>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_indicators_cstdint.cmake"
    UPDATE_DISCONNECTED TRUE
  )
  FetchContent_MakeAvailable(indicators)
  FetchContent_GetProperties(indicators SOURCE_DIR CSVZALL_INDICATORS_SOURCE_DIR)
  if(CSVZALL_INDICATORS_SOURCE_DIR)
    execute_process(
      COMMAND ${CMAKE_COMMAND}
        -D "INDICATORS_SOURCE_DIR=${CSVZALL_INDICATORS_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_indicators_cstdint.cmake"
      COMMAND_ERROR_IS_FATAL ANY)
  endif()

  set(CSVZALL_INDICATORS_TARGET indicators)
  if(TARGET indicators::indicators)
    set(CSVZALL_INDICATORS_TARGET indicators::indicators)
  endif()

endif()
