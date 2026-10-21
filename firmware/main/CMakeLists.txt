idf_component_register(SRCS "main.c"
                            "sdr_control.c"
                            "sdr_stream.c"
                            "bsp_bringup.c"
                            "dsp/iq_ring.c"
                    INCLUDE_DIRS "."
                                  "dsp"
                    REQUIRES
                        waveshare__esp32_p4_wifi6_touch_lcd_4b
                        hardcoreerik__esp-rtl-sdr
                        lvgl__lvgl
                        esp-dsp
                        esp_driver_i2s
                        esp_driver_i2c
                        esp_driver_gpio
                        esp_driver_ledc
                        nvs_flash
                        esp_event
                        esp_timer
                        freertos)
