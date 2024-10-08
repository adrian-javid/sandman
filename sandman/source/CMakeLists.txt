include(GNUInstallDirs)

set(SOURCE_FILES command.cpp config.cpp control.cpp input.cpp logger.cpp mqtt.cpp notification.cpp
	 reports.cpp schedule.cpp shell.cpp timer.cpp)
add_library(sandman_lib STATIC ${SOURCE_FILES})
add_executable(sandman main.cpp)

add_library(sandman_compiler_flags INTERFACE)
target_compile_features(sandman_compiler_flags INTERFACE cxx_std_17)

option(ENABLE_GPIO "Whether to use Raspberry Pi GPIO or not." ON)
message(STATUS "ENABLE_GPIO = ${ENABLE_GPIO}")
if (ENABLE_GPIO)
    target_compile_definitions(sandman_lib PUBLIC ENABLE_GPIO)
endif()

target_compile_definitions(sandman_lib 
                           PUBLIC SANDMAN_CONFIG_DIR="${CMAKE_INSTALL_FULL_SYSCONFDIR}/sandman/"
                           PUBLIC SANDMAN_TEMP_DIR="${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/sandman/")

#configure_file(sandman_config.h.in sandman_config.h)

find_package(Curses REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(Mosquitto IMPORTED_TARGET libmosquitto REQUIRED)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
   target_compile_options(sandman_lib PUBLIC 
      -Wall                    # Enable most common warnings
      -Wextra                  # Enable additional warnings
      -Werror                  # Treat warnings as errors
      -Wpedantic               # Enforce strict ISO C++ compliance
      -Wshadow                 # Warn when a variable shadows another
      -Wnon-virtual-dtor       # Warn if a class with virtual functions has a non-virtual destructor
      -Wold-style-cast         # Warn about old-style casts
      -Woverloaded-virtual     # Warn if a virtual function is overloaded
      # -Wsign-conversion      # Warn on implicit conversions that change sign
      -Wformat=1               # Warn about format string issues
      -Wfloat-equal            # Warn if floating-point values are compared for equality
		-Wimplicit-fallthrough   # Warn if switch case fallthrough not marked with `[[fallthrough]]`
   )

   # `-Wformat=2` would warn about non literal format strings.

else()
   message(WARNING "Not using GNU compiler or Clang compiler.")
endif()

target_include_directories(sandman_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
#target_include_directories(sandman_lib PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(sandman_lib PUBLIC sandman_compiler_flags ${CURSES_LIBRARIES} 
                      PkgConfig::Mosquitto)
if (ENABLE_GPIO)
    target_link_libraries(sandman_lib PUBLIC pigpio)
endif()

target_link_libraries(sandman PUBLIC sandman_compiler_flags sandman_lib ${CURSES_LIBRARIES})

install(TARGETS sandman DESTINATION bin)