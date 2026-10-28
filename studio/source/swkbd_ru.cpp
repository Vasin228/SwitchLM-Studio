#include "swkbd_ru.hpp"
#include <switch.h>
#include <cstring>

std::string SwkbdRu::show() {
    SwkbdConfig swkbd;
    Result rc = swkbdCreate(&swkbd, 0);
    if (R_FAILED(rc)) return "";

    swkbdConfigMakePresetDefault(&swkbd);
    // SwkbdType_Normal enables system native Russian layout and the Globe language switcher button
    swkbdConfigSetType(&swkbd, SwkbdType_Normal);
    swkbdConfigSetInitialCursorPos(&swkbd, 1);
    swkbdConfigSetReturnButtonFlag(&swkbd, 1);
    swkbdConfigSetBlurBackground(&swkbd, 1);
    swkbdConfigSetGuideText(&swkbd, "Введите запрос для SwitchLM (Русский / English)...");
    swkbdConfigSetOkButtonText(&swkbd, "Готово");

    char buf[512] = {0};
    rc = swkbdShow(&swkbd, buf, sizeof(buf));
    swkbdClose(&swkbd);

    if (R_SUCCEEDED(rc)) {
        return std::string(buf);
    }
    return "";
}
