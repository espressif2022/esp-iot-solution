# Select this directory INSTEAD OF the V1 esp_display_present component.
# The identical component name makes the implementations mutually exclusive.
idf_component_register(SRCS src/facade.c src/resources.c ../../../esp_display_present/src/platform/present_te.c INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS src ../../src ../../../esp_display_present/private_include
    PRIV_REQUIRES esp_display_present_v2 esp_driver_gpio esp_timer esp_system heap)
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
