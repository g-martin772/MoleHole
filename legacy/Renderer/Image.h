#pragma once
#include <string>

class Image {
public:
    unsigned int textureID;
    int width, height;

    Image(int width, int height);
    ~Image();

    static Image* LoadHDR(const std::string& filepath);

private:
    Image(unsigned int textureID, int width, int height);
};
