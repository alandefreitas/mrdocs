#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
#
# Official repository: https://github.com/cppalliance/mrdocs
#

# Runs the command after `--` and checks both its exit status and its output.
# ctest's PASS_REGULAR_EXPRESSION looks at the output alone and ignores the
# exit status, so it cannot tell a run that stopped from one that went on.
#
#   cmake -D OUTCOME=<success|failure> -D REGEX=<regular expression>
#         -P RunChecked.cmake -- <command> [<argument>...]
#
# OUTCOME says whether the command must exit with zero (success) or with
# status 1, the one `mrdocs` returns when it refuses to continue (failure).
# Any other status, such as the code of a crash, fails the script. The script
# also fails unless the combined standard output and standard error match
# REGEX.

set(command)
set(past_separator FALSE)
math(EXPR last "${CMAKE_ARGC} - 1")
foreach (index RANGE 0 ${last})
    if (past_separator)
        list(APPEND command "${CMAKE_ARGV${index}}")
    elseif (CMAKE_ARGV${index} STREQUAL "--")
        set(past_separator TRUE)
    endif ()
endforeach ()
if (NOT command)
    message(FATAL_ERROR "RunChecked.cmake: no command after --")
endif ()
if (NOT OUTCOME STREQUAL "success" AND NOT OUTCOME STREQUAL "failure")
    message(FATAL_ERROR "RunChecked.cmake: OUTCOME must be success or failure")
endif ()

execute_process(
    COMMAND ${command}
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
message("${output}")

if (OUTCOME STREQUAL "success" AND NOT status EQUAL 0)
    message(FATAL_ERROR "FAIL: expected exit status 0, got ${status}")
endif ()
if (OUTCOME STREQUAL "failure" AND NOT status STREQUAL "1")
    message(FATAL_ERROR "FAIL: expected exit status 1, got ${status}")
endif ()
if (NOT output MATCHES "${REGEX}")
    message(FATAL_ERROR "FAIL: the output does not match: ${REGEX}")
endif ()
