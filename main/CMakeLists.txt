idf_component_register(
	SRCS "src/smart_led_mqtt.c" "src/led_strip_encoder.c" "src/smart_led_main.c"
	INCLUDE_DIRS "include"
)

set(ENV_FILE ${CMAKE_SOURCE_DIR}/.env)
set(GENERATED_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/env_config.h)

add_custom_command(
    OUTPUT ${GENERATED_HEADER}
    COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/generate_env_header.py ${CMAKE_SOURCE_DIR}
    DEPENDS ${ENV_FILE}
    COMMENT "Generating env_config.h from .env"
)

add_custom_target(generate_env_header DEPENDS ${GENERATED_HEADER})
add_dependencies(${COMPONENT_LIB} generate_env_header)

# --- Step 5: Add current dir (where header is generated) to include paths ---
target_include_directories(${COMPONENT_LIB} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})