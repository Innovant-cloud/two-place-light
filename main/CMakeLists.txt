idf_component_register(
    SRCS "main.c"
         "solar.c"
         "civil_time.c"
         "places.c"
         "app_clock.c"
         "app_state.c"
         "app_setup.c"
         "ui.c"
    INCLUDE_DIRS "."
    REQUIRES bsp nvs_flash
)

# 基线 main 的测试菜单与 demo_*.c 仍留在树里作参考，但不参与本应用的构建。
# 派生应用禁止沿用那套测试 UI，详见 docs/development/ai-guide.md。
