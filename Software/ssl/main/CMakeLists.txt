idf_component_register(SRCS "app_main.c"
                    PRIV_REQUIRES mqtt esp_partition nvs_flash esp_netif app_update esp_driver_tsens esp_driver_ledc
                    INCLUDE_DIRS ".")
