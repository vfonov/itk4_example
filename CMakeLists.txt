# CMakeFiles.txt for the EZminc
#
# Vladimir S. FONOV - vladimir.fonov@gmail.com

PROJECT(ITK4_EXAMPLE)
CMAKE_MINIMUM_REQUIRED(VERSION 2.8)

FIND_PACKAGE(ITK REQUIRED)
include(${ITK_USE_FILE})
LINK_DIRECTORIES(${ITK_LIBRARY_DIRS})

IF(NOT ITK_FOUND OR NOT ITK_VERSION_MAJOR VERSION_EQUAL 4)
  message("ITK version 4 with MINC support required!")
ENDIF(NOT ITK_FOUND OR NOT ITK_VERSION_MAJOR VERSION_EQUAL 4)


ADD_SUBDIRECTORY(example)
