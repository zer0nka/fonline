# Copyright (c) 2013 Alexander Konovalov. All rights reserved.
# Use of this source code is governed by a GPL-style license that can be
# found in the LICENSE file.

project(FOnlineFileManager)

# List all the .h (header) files in the module in the alphabetic order.
set(LIB_HXX
  FileManager.hpp
)

# List all the non-test .cpp files in the alphabetic order.
set(LIB_CXX
  FileManager.cpp
)

# List all the *_test.cpp files in the alphabetic order.
set(TEST_CXX

)

add_library(FOnlineFileManager ${LIB_CXX} ${LIB_HXX})
IF("${CMAKE_SYSTEM_NAME}" STREQUAL "Windows")
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DFONLINE_FILEMANAGER_EXPORTS")
ENDIF()

include_directories(..)
target_link_libraries(FOnlineFileManager DatArchive)
