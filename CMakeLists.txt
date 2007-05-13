PROJECT(battery_monitor C)

# Xine commands to run
SET(XINE_CONFIG_CFLAGS_COMMAND xine-config --cflags)
SET(XINE_CONFIG_LIBS_COMMAND xine-config --libs)

# Try to get needed compilation and linker flags for Xine
EXECUTE_PROCESS(COMMAND ${XINE_CONFIG_CFLAGS_COMMAND}
		RESULT_VARIABLE xine_config_cflags_result
		OUTPUT_VARIABLE xine_config_cflags_output)

EXECUTE_PROCESS(COMMAND ${XINE_CONFIG_LIBS_COMMAND}
		RESULT_VARIABLE xine_config_libs_result
		OUTPUT_VARIABLE xine_config_libs_output)

# Check errors running Xine commands
IF (xine_config_cflags_result)
	MESSAGE(FATAL_ERROR "Error: could not run ${XINE_CONFIG_CFLAGS_COMMAND}")
ENDIF(xine_config_cflags_result)

IF (xine_config_libs_result)
	MESSAGE(FATAL_ERROR "Error: could not run ${XINE_CONFIG_LIBS_COMMAND}")
ENDIF(xine_config_libs_result)

# Extract linker libs and linker directories from the Xine commands
STRING(REGEX REPLACE "(-L[^\ \t]+)|((^|\ |\t)-l)" " " xine_libs "${xine_config_libs_output}")
STRING(REGEX REPLACE "(-l[^\ \t]+)|((^|\ |\t)-L)" " " xine_lib_dirs "${xine_config_libs_output}")

# Add Xine compiler flags
SEPARATE_ARGUMENTS(xine_config_cflags_output)
ADD_DEFINITIONS(${xine_config_cflags_output})

# Add main program
ADD_EXECUTABLE(battery_monitor battery_monitor.c)

# Add Xine linker flags
SEPARATE_ARGUMENTS(xine_lib_dirs)
SEPARATE_ARGUMENTS(xine_libs)
LINK_DIRECTORIES(${xine_lib_dirs})
TARGET_LINK_LIBRARIES(battery_monitor ${xine_libs})

# Find X11 and pthread libraries
FIND_LIBRARY(LOCAL_X11_LIB X11)
IF (NOT LOCAL_X11_LIB)
	MESSAGE(FATAL_ERROR "Error: X11 library not found")
ENDIF(NOT LOCAL_X11_LIB)
TARGET_LINK_LIBRARIES(battery_monitor ${LOCAL_X11_LIB})

FIND_LIBRARY(LOCAL_PTHREAD_LIB pthread)
IF (NOT LOCAL_PTHREAD_LIB)
	MESSAGE(FATAL_ERROR "Error: pthread library not found")
ENDIF(NOT LOCAL_PTHREAD_LIB)
TARGET_LINK_LIBRARIES(battery_monitor ${LOCAL_PTHREAD_LIB})

# Install targets
INSTALL(TARGETS battery_monitor RUNTIME DESTINATION bin)
