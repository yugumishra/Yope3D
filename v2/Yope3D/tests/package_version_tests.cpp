//testing cmake building process
#include <iostream>
#include <GLFW/glfw3.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <AL/al.h>
#include <AL/alc.h>
#include <stb_image.h>
#include <stb_vorbis_wrapper.h>

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

    // 4. Test stb_image
    stbi_set_flip_vertically_on_load(false);
    std::cout << "stb_image: Available" << std::endl;

    // 5. Test stb_vorbis
    // Just verify declarations are available (implementation is in stb_impl.cpp)
    // stb_vorbis_decode_filename and stb_vorbis_decode_memory are declared
    std::cout << "stb_vorbis: Available" << std::endl;

    std::cout << "Yope3D Infrastructure: READY" << std::endl;
    return 0;
}