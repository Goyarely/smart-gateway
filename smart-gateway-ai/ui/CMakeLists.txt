SET(SOURCES screens/ui_Screen1.c
    screens/ui_Screen2.c
    screens/ui_Screen3.c
    screens/ui_Screen4.c
    screens/ui_Screen5.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_673166940.c
    images/ui_img_1663005091.c
    images/ui_img_399794486.c
    images/ui_img_1887403499.c
    images/ui_img_237420965.c
    images/ui_img_84551563.c
    images/ui_img_688870236.c
    images/ui_img_181976582.c
    images/ui_img_994938910.c
    fonts/ui_font_Font1.c
    fonts/ui_font_Font22.c
    fonts/ui_font_Font25.c)

add_library(ui ${SOURCES})
