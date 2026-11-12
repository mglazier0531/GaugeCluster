idf_component_register(SRCS "main.cpp" "rgb_lcd_port.cpp" "io_extension.cpp" "i2c.cpp" "dashboard_assets.cpp" INCLUDE_DIRS "." "../include" REQUIRES esp_lcd driver esp_timer)
