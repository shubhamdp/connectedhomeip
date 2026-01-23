set(require_list chip driver)

if (IDF_VERSION_MAJOR GREATER_EQUAL 6)
    list(APPEND require_list esp_driver_ledc mbedtls)
endif()

if(${IDF_TARGET} STREQUAL "esp32")
    list(APPEND require_list tft)
endif()

idf_component_register(SRCS "Display.cpp" 
                            "ScreenManager.cpp" 
                            "Screen.cpp" 
                            "ListScreen.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES ${require_list})
