
if (NOT BOOST_CUSTOM)
    find_package (PythonLibs ${PYTHON_VERSION} REQUIRED)
    find_package (Boost 1.34 REQUIRED COMPONENTS filesystem program_options regex system thread python)
endif ()

if ( BOOST_CUSTOM OR Boost_FOUND AND PYTHONLIBS_FOUND)

    set (python_srcs py_imageinput.cpp py_imageoutput.cpp
         py_imagecache.cpp py_imagespec.cpp
         py_imagebuf.cpp py_typedesc.cpp py_paramvalue.cpp
         py_oiio.cpp)

    message (STATUS "Python found ${PYTHONLIBS_FOUND} ")
    message (STATUS "Python include dirs ${PYTHON_INCLUDE_PATH}")
    message (STATUS "Python libraries    ${PYTHON_LIBRARIES}")

    include_directories (${PYTHON_INCLUDE_PATH} ${Boost_INCLUDE_DIRS})
    add_library (PyOpenImageIO SHARED ${python_srcs})
    target_link_libraries (PyOpenImageIO OpenImageIO ${Boost_LIBRARIES} ${Boost_Python_LIBRARIES} ${PYTHON_LIBRARIES} ${CMAKE_DL_LIBS})

    set_target_properties (PyOpenImageIO PROPERTIES
                             OUTPUT_NAME OpenImageIO
                             PREFIX "")
    install (TARGETS PyOpenImageIO DESTINATION ${PYLIBDIR})

endif ()
