SET(SOURCES screens/ui_Screen1.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_1299355463.c
    images/ui_img_384279421.c
    images/ui_img_1658401886.c
    images/ui_img_1731705873.c
    images/ui_img_1226051476.c
    images/ui_img_310975434.c
    images/ui_img_tempiconor20x20_png.c
    images/ui_img_windicon20x20_png.c
    images/ui_img_humidiconor20x20_png.c
    images/ui_img_logo20x20_png.c)

add_library(ui ${SOURCES})
