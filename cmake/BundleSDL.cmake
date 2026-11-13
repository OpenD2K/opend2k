# Post-build step for "Dune 2000.app": copy libSDL3 into Contents/Frameworks, point the
# executable at @rpath/<soname> and ad-hoc re-sign (install_name_tool invalidates signatures).
# Invoked with -DAPP=<bundle dir> -DEXE=<executable> -DSDL=<libSDL3 path> -DSONAME=<file name>.
set(fw "${APP}/Contents/Frameworks")
file(MAKE_DIRECTORY "${fw}")
get_filename_component(real "${SDL}" REALPATH)
file(COPY_FILE "${real}" "${fw}/${SONAME}" ONLY_IF_DIFFERENT)
file(CHMOD "${fw}/${SONAME}" PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
execute_process(COMMAND install_name_tool -id "@rpath/${SONAME}" "${fw}/${SONAME}" COMMAND_ERROR_IS_FATAL ANY)
# Whatever install name the build linked against (e.g. /opt/homebrew/opt/sdl3/lib/...).
execute_process(COMMAND otool -L "${EXE}" OUTPUT_VARIABLE deps COMMAND_ERROR_IS_FATAL ANY)
string(REGEX MATCH "[^\t\n ]*${SONAME}" old "${deps}")
if(old AND NOT old STREQUAL "@rpath/${SONAME}")
    execute_process(COMMAND install_name_tool -change "${old}" "@rpath/${SONAME}" "${EXE}" COMMAND_ERROR_IS_FATAL ANY)
endif()
execute_process(COMMAND codesign --force --sign - "${fw}/${SONAME}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND codesign --force --sign - "${APP}" COMMAND_ERROR_IS_FATAL ANY)
