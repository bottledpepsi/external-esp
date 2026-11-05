// tests/fake/Gfx.h - a recording stand-in for src/Gfx.h so the renderer's logic (EspRenderer.cpp) runs in the host tests.
// It has the same names as the real class for the calls the renderer makes; text is 6 px per character, 1 px of height per font point,
// and every call is appended to a log the test reads. Nothing is drawn.
#pragma once
#include <string>
#include <vector>

struct Image;
struct Col
{
    float r = 1, g = 1, b = 1, a = 1;
    Col with(float alpha) const { return {r, g, b, alpha}; }
};
enum class Font { Name10, Name13, Name17, Ui, UiSmall, UiBold, Hint, Count };

struct TextCall
{
    std::string text;
    float x, y; // y = the top of the line; x = the centre (TextCentered) or the left edge (Text)
    Font font;
    Col col;
};

class Gfx
{
public:
    std::vector<TextCall> texts;
    int lines = 0, rects = 0, images = 0;
    struct Seg { float x0, y0, x1, y1; Col col; };
    struct Disc { float x, y, r; Col col; };
    struct Box { float x, y, w, h; };
    std::vector<Seg> segs; // every Line(): where, and in what color
    std::vector<Disc> discs; // every FillCircle()
    std::vector<Box> panels; // every FillRRect() (the radar's background, labels' plates ...)

    void Line(float x0, float y0, float x1, float y1, Col c, float = 1.f) { ++lines; segs.push_back({x0, y0, x1, y1, c}); }
    void Rect(float, float, float, float, Col, float = 1.f) { ++rects; }
    void FillRect(float, float, float, float, Col) { ++rects; }
    void RRect(float, float, float, float, float, Col, float = 1.f) { ++rects; }
    void FillRRect(float x, float y, float w, float h, float, Col) { ++rects; panels.push_back({x, y, w, h}); }
    void Circle(float, float, float, Col, float = 1.f) { ++rects; }
    void FillCircle(float x, float y, float r, Col c) { ++rects; discs.push_back({x, y, r, c}); }
    void FillTriangle(float, float, float, float, float, float, Col) { ++rects; }
    void DrawImage(const Image&, float, float, float, float, float = 1.f) { ++images; }
    void PushClip(float, float, float, float) {}
    void PopClip() {}
    void Text(const std::string& s, float x, float y, Font f, Col c, bool = false) { texts.push_back({s, x, y, f, c}); }
    void TextCentered(const std::string& s, float cx, float y, Font f, Col c, bool = false) { texts.push_back({s, cx, y, f, c}); }
    void TextRight(const std::string& s, float rx, float y, Font f, Col c, bool = false) { texts.push_back({s, rx, y, f, c}); }
    float TextWidth(const std::string& s, Font) { return 6.f * static_cast<float>(s.size()); }
};
