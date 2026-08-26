# gen_hpp.cmake — re-embed echosdk.h declarations into echosdk.hpp
# Called by CMakeLists.txt; do not invoke directly.
# Required variables (passed via cmake -D):
#   C_HEADER    — path to include/echosdk.h
#   CPP_HEADER  — path to bindings/cpp/echosdk.hpp

file(READ "${C_HEADER}"   c_text)
file(READ "${CPP_HEADER}" hpp)

string(REPLACE "\r\n" "\n" c_text "${c_text}")

# Strip opening header guard (may be preceded by a doc-comment block)
string(REGEX REPLACE
    "(^|\n)[ \t]*#ifndef[ \t]+ECHOSDK_H[ \t]*\n[ \t]*#define[ \t]+ECHOSDK_H[ \t]*\n"
    "\\1" body "${c_text}")

# Strip closing header guard (last #endif line) + any trailing blank lines
string(REGEX REPLACE
    "\n[ \t]*#endif[^\n]*([ \t]*\n)*[ \t]*$"
    "\n" body "${body}")

# Splice into .hpp between the embedded-C markers
set(_start "#ifndef ECHOSDK_H\n")
set(_end   "#endif /* ECHOSDK_H — embedded C declarations end */")

string(FIND "${hpp}" "${_start}" _s)
string(FIND "${hpp}" "${_end}"   _e)

if(_s EQUAL -1 OR _e EQUAL -1)
    message(FATAL_ERROR "Markers not found in ${CPP_HEADER}")
endif()

string(LENGTH "${_end}" _elen)
math(EXPR _after "${_e} + ${_elen}")

string(SUBSTRING "${hpp}" 0         ${_s}     _before)
string(SUBSTRING "${hpp}" ${_after} -1        _after_text)

set(new_hpp "${_before}${_start}${body}${_end}${_after_text}")

if(new_hpp STREQUAL hpp)
    message(STATUS "echosdk.hpp is already up to date.")
else()
    file(WRITE "${CPP_HEADER}" "${new_hpp}")
    message(STATUS "Updated ${CPP_HEADER}")
endif()
