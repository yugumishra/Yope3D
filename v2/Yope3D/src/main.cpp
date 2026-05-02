//testing cmake building process
#include <iostream>
#include <GLFW/glfw3.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <AL/al.h>
#include <AL/alc.h>

int main() {
    // 1. Test GLFW
    std::cout << "GLFW Version: " << glfwGetVersionString() << std::endl;

    // 2. Test FreeType
    FT_Library ft;
    if (!FT_Init_FreeType(&ft)) {
        FT_Int major, minor, patch;
        FT_Library_Version(ft, &major, &minor, &patch);
        std::cout << "FreeType Version: " << major << "." << minor << "." << patch << std::endl;
        FT_Done_FreeType(ft);
    }

    // 3. Test OpenAL
    const ALCchar* deviceName = alcGetString(NULL, ALC_DEVICE_SPECIFIER);
    std::cout << "OpenAL Default Device: " << (deviceName ? deviceName : "None") << std::endl;

    std::cout << "Yope3D Infrastructure: READY" << std::endl;
    return 0;
}