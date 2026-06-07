idf_component_register(
    SRCS "EspServer.c"
    INCLUDE_DIRS "." "./libs"
)

spiffs_create_partition_image(storage spiffs FLASH_IN_PROJECT)