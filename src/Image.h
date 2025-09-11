#pragma once

class Image {
public:
    unsigned int textureID;
    int width, height;
    Image(int width, int height);
    ~Image();
};

