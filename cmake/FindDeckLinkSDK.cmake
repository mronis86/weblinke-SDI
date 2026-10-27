# Finds the Blackmagic DeckLink SDK headers.
#
# The SDK is a free but licence-gated download from Blackmagic and is not ours
# to redistribute, so nothing from it is vendored here. Point the build at your
# own copy:
#
#   -DDECKLINK_SDK_DIR="/path/to/Blackmagic DeckLink SDK 16.0"
#
# Accepted layouts:
#   <dir>/Mac/include/DeckLinkAPI.h     (official SDK archive)
#   <dir>/Linux/include/DeckLinkAPI.h
#   <dir>/Win/include/DeckLinkAPI.h     (older SDKs, or after midl)
#   <dir>/Win/include/DeckLinkAPI.idl   (SDK 14+ Windows — headers generated)
#   <dir>/DeckLinkAPI.h                 (a bare include directory)
#
# On macOS and Linux the API is reached through DeckLinkAPIDispatch.cpp, which
# ships in the SDK and dlopens the installed DeckLinkAPI framework/library. That
# is why there is no link library on those platforms: an executable built with
# the SDK still runs on a machine with no Desktop Video installed, and simply
# finds no devices.
#
# On Windows the API is native COM. CreateDeckLinkIteratorInstance() does not
# exist; callers use CoCreateInstance(CLSID_CDeckLinkIterator, ...). Recent
# SDKs ship only DeckLinkAPI.idl on Windows — this module runs midl to produce
# DeckLinkAPI.h and DeckLinkAPI_i.c into the build tree. ole32/oleaut32 provide
# CoCreateInstance and the BSTR helpers GetDisplayName needs.
#
# Sets: DeckLinkSDK_FOUND, DeckLinkSDK_INCLUDE_DIRS,
#       DeckLinkSDK_DISPATCH_SOURCES, DeckLinkSDK_LIBRARIES

set(_dl_hints
  "${DECKLINK_SDK_DIR}"
  "$ENV{DECKLINK_SDK_DIR}"
)

# Prefer the platform's own include tree. A full SDK archive contains
# Mac/Linux/Win side by side; searching Mac first on Windows silently picks
# CFString headers and then fails to find DeckLinkAPI_i.c.
if(WIN32)
  set(_dl_path_suffixes
    Win/include include Win
    "Blackmagic DeckLink SDK/Win/include"
  )
elseif(APPLE)
  set(_dl_path_suffixes
    Mac/include include Mac
    "Blackmagic DeckLink SDK/Mac/include"
  )
else()
  set(_dl_path_suffixes
    Linux/include include Linux
    "Blackmagic DeckLink SDK/Linux/include"
  )
endif()

find_path(DeckLinkSDK_INCLUDE_DIR
  NAMES DeckLinkAPI.h
  HINTS ${_dl_hints}
  PATH_SUFFIXES ${_dl_path_suffixes}
  NO_DEFAULT_PATH
  DOC "Directory containing DeckLinkAPI.h"
)

# SDK 14+ on Windows ships IDL only. Locate that and generate headers into
# the build tree so FindDeckLinkSDK still works against a clean extract.
if(NOT DeckLinkSDK_INCLUDE_DIR AND WIN32)
  find_path(DeckLinkSDK_IDL_DIR
    NAMES DeckLinkAPI.idl
    HINTS ${_dl_hints}
    PATH_SUFFIXES Win/include include Win
    NO_DEFAULT_PATH
    DOC "Directory containing DeckLinkAPI.idl"
  )
  if(DeckLinkSDK_IDL_DIR)
    find_program(MIDL_EXECUTABLE midl
      HINTS
        "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64"
        "$ENV{WindowsSdkDir}/bin/x64"
      DOC "Microsoft Interface Definition Language compiler"
    )
    if(NOT MIDL_EXECUTABLE)
      # Fall back to the newest kits midl on a stock Build Tools install.
      file(GLOB _midl_candidates
        "C:/Program Files*/Windows Kits/10/bin/*/x64/midl.exe")
      if(_midl_candidates)
        list(SORT _midl_candidates ORDER DESCENDING)
        list(GET _midl_candidates 0 MIDL_EXECUTABLE)
      endif()
    endif()

    if(MIDL_EXECUTABLE)
      set(_dl_gen_dir "${CMAKE_BINARY_DIR}/decklink_sdk_gen")
      file(MAKE_DIRECTORY "${_dl_gen_dir}")
      set(_dl_gen_h "${_dl_gen_dir}/DeckLinkAPI.h")
      set(_dl_gen_i "${_dl_gen_dir}/DeckLinkAPI_i.c")

      if(NOT EXISTS "${_dl_gen_h}" OR NOT EXISTS "${_dl_gen_i}")
        message(STATUS "DeckLink SDK: generating Windows headers with midl "
                       "(${MIDL_EXECUTABLE})")
        execute_process(
          COMMAND "${MIDL_EXECUTABLE}"
                  /W1 /nologo /char signed /env x64
                  /h DeckLinkAPI.h
                  /iid DeckLinkAPI_i.c
                  /notlb
                  "${DeckLinkSDK_IDL_DIR}/DeckLinkAPI.idl"
          WORKING_DIRECTORY "${_dl_gen_dir}"
          RESULT_VARIABLE _dl_midl_rc
          OUTPUT_VARIABLE _dl_midl_out
          ERROR_VARIABLE _dl_midl_err
        )
        if(NOT _dl_midl_rc EQUAL 0)
          message(WARNING "DeckLink SDK: midl failed (${_dl_midl_rc}): "
                          "${_dl_midl_err}${_dl_midl_out}")
        endif()
      endif()

      if(EXISTS "${_dl_gen_h}")
        # Version header stays in the SDK tree; generated COM headers live here.
        set(DeckLinkSDK_INCLUDE_DIR "${_dl_gen_dir}")
        set(DeckLinkSDK_IDL_INCLUDE_DIR "${DeckLinkSDK_IDL_DIR}")
        set(DeckLinkSDK_DISPATCH_SOURCES "${_dl_gen_i}")
      endif()
    else()
      message(WARNING "DeckLink SDK: Win/include has DeckLinkAPI.idl but midl "
                      "was not found — install Windows SDK / Build Tools.")
    endif()
  endif()
endif()

if(NOT DEFINED DeckLinkSDK_DISPATCH_SOURCES)
  set(DeckLinkSDK_DISPATCH_SOURCES "")
endif()
set(DeckLinkSDK_LIBRARIES "")

if(DeckLinkSDK_INCLUDE_DIR)
  if(WIN32)
    # COM GUID definitions for CLSID_CDeckLinkIterator / IID_IDeckLink*.
    if(NOT DeckLinkSDK_DISPATCH_SOURCES)
      if(EXISTS "${DeckLinkSDK_INCLUDE_DIR}/DeckLinkAPI_i.c")
        set(DeckLinkSDK_DISPATCH_SOURCES
            "${DeckLinkSDK_INCLUDE_DIR}/DeckLinkAPI_i.c")
      else()
        message(WARNING "DeckLink SDK: DeckLinkAPI_i.c missing from "
                        "${DeckLinkSDK_INCLUDE_DIR} — Windows link errors are likely.")
      endif()
    endif()
    list(APPEND DeckLinkSDK_LIBRARIES ole32 oleaut32)
  else()
    # Required on macOS and Linux: it provides CreateDeckLinkIteratorInstance()
    # and friends by dlopening the driver's library.
    if(EXISTS "${DeckLinkSDK_INCLUDE_DIR}/DeckLinkAPIDispatch.cpp")
      set(DeckLinkSDK_DISPATCH_SOURCES
          "${DeckLinkSDK_INCLUDE_DIR}/DeckLinkAPIDispatch.cpp")
    else()
      message(WARNING "DeckLink SDK: DeckLinkAPIDispatch.cpp missing from "
                      "${DeckLinkSDK_INCLUDE_DIR} — link errors are likely.")
    endif()
  endif()

  if(APPLE)
    find_library(_dl_corefoundation CoreFoundation REQUIRED)
    list(APPEND DeckLinkSDK_LIBRARIES ${_dl_corefoundation})
  elseif(UNIX AND NOT WIN32)
    list(APPEND DeckLinkSDK_LIBRARIES ${CMAKE_DL_LIBS})
  endif()

  # Report the SDK version so a build log records what it was compiled against.
  set(_dl_version_candidates
    "${DeckLinkSDK_INCLUDE_DIR}/DeckLinkAPIVersion.h"
    "${DeckLinkSDK_IDL_INCLUDE_DIR}/DeckLinkAPIVersion.h"
  )
  foreach(_dl_ver_file IN LISTS _dl_version_candidates)
    if(EXISTS "${_dl_ver_file}")
      file(STRINGS "${_dl_ver_file}" _ver
           REGEX "BLACKMAGIC_DECKLINK_API_VERSION_STRING")
      string(REGEX MATCH "\"([0-9.]+)\"" _m "${_ver}")
      set(DeckLinkSDK_VERSION "${CMAKE_MATCH_1}")
      break()
    endif()
  endforeach()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(DeckLinkSDK
  REQUIRED_VARS DeckLinkSDK_INCLUDE_DIR
  VERSION_VAR DeckLinkSDK_VERSION
  FAIL_MESSAGE "DeckLink SDK not found. Pass -DDECKLINK_SDK_DIR=<sdk root>."
)

if(DeckLinkSDK_FOUND)
  set(DeckLinkSDK_INCLUDE_DIRS "${DeckLinkSDK_INCLUDE_DIR}")
  if(DeckLinkSDK_IDL_INCLUDE_DIR)
    list(APPEND DeckLinkSDK_INCLUDE_DIRS "${DeckLinkSDK_IDL_INCLUDE_DIR}")
  endif()
endif()

mark_as_advanced(DeckLinkSDK_INCLUDE_DIR DeckLinkSDK_IDL_DIR MIDL_EXECUTABLE)
