vgc_test_library(core
    CPP_TESTS
        test_arithmetic.cpp
        test_array.cpp
        test_datetime.cpp
        test_enum.cpp
        test_flags.cpp
        test_format.cpp
        test_int.cpp
        test_object.cpp
        test_parse.cpp
        test_preprocessor.cpp
        test_signal.cpp
        test_span.cpp
        test_typeid.cpp
        test_zero.cpp

    PYTHON_TESTS
        test_arithmetic.py
        test_arrays.py
        test_color.py
        test_format.py
        test_object.py
        test_parse.py
        test_paths.py
        test_signal.py
        test_stringid.py
        test_xml.py
)
