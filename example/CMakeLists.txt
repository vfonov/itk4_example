IF(ITK_FOUND)

  LINK_DIRECTORIES(${ITK_LIBRARY_DIRS} )
  INCLUDE_DIRECTORIES(${ITK_INCLUDE_DIRS} )

  ADD_EXECUTABLE( itk_resample_example itk_resample.cpp )
  TARGET_LINK_LIBRARIES(itk_resample_example ${ITK_LIBRARIES})

  INSTALL(TARGETS 
    itk_resample_example  
   RUNTIME DESTINATION bin )

ENDIF(ITK_FOUND)
