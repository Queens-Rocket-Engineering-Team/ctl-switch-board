set(CONFIG_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/switch_config_generate.py)
set(SWITCH_CONFIG ${CMAKE_SOURCE_DIR}/switch_config.json)

set(CONFIG_HEADER ${CMAKE_CURRENT_BINARY_DIR}/switch_config.h)
set(CONFIG_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/switch_config.c)

idf_component_register(
    SRCS "${CONFIG_SOURCE}"
    INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES
    switches
    PRIV_REQUIRES
    esp_tinyusb
)

idf_build_get_property(python PYTHON)
# convert JSON config file to header and source code
add_custom_command(
    OUTPUT "${CONFIG_HEADER}" "${CONFIG_SOURCE}"
    COMMAND ${python} "${CONFIG_SCRIPT}"
            "${SWITCH_CONFIG}"
            "${CONFIG_HEADER}"
            "${CONFIG_SOURCE}"
    DEPENDS "${SWITCH_CONFIG}"
            "${CONFIG_SCRIPT}"
    VERBATIM
    COMMENT "Generating switch_config component"
)

set_source_files_properties("${CONFIG_SOURCE}" PROPERTIES GENERATED TRUE)