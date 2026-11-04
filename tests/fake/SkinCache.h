// tests/fake/SkinCache.h - no skins in the host tests: the avatar placeholder (a letter) is what gets drawn.
#pragma once
#include "Image.h"
#include <memory>
#include <string>

namespace SkinCache
{
    inline void Request(const std::string&, const std::string&) {}
    inline std::shared_ptr<const Image> Get(const std::string&) { return nullptr; }
}
