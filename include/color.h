//========= Copyright (c) 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#pragma once


#include <algorithm>

#include "tier0/basetypes.h"

//-----------------------------------------------------------------------------
// Purpose: Basic handler for an rgb set of colors
//			This class is fully inline
//-----------------------------------------------------------------------------
class Color {
public:
    // constructors
    Color() {
        SetColor(0.f, 0.f, 0.f, 0.f);
    }

    Color(const uint8_t _r, const uint8_t _g, const uint8_t _b) {
        SetColor(_r, _g, _b, 0u);
    }

    Color(const uint8_t _r, const uint8_t _g, const uint8_t _b, const uint8_t _a) {
        SetColor(_r, _g, _b, _a);
    }

    Color(const int _r, const int _g, const int _b, const int _a) {
        SetColor(static_cast<uint8_t>(_r), static_cast<uint8_t>(_g), static_cast<uint8_t>(_b), static_cast<uint8_t>(_a));
    }

    Color(const float32 _r, const float32 _g, const float32 _b) {
        SetColor(_r, _g, _b, 0.f);
    }

    // Still in 0.f-255.f range
    Color(const float32 _r, const float32 _g, const float32 _b, const float32 _a) {
        SetColor(_r, _g, _b, _a);
    }

    // set the color
    // r - red component (0-255)
    // g - green component (0-255)
    // b - blue component (0-255)
    // a - alpha component, controls transparency (0 - transparent, 255 - opaque);

    void SetColor(const float32 _r, const float32 _g, const float32 _b, const float32 _a = 0) {
        _color[0] = static_cast<uint8_t>(std::clamp(_r, 0.f,255.f));
        _color[1] = static_cast<uint8_t>(std::clamp(_g, 0.f,255.f));
        _color[2] = static_cast<uint8_t>(std::clamp(_b, 0.f,255.f));
        _color[3] = static_cast<uint8_t>(std::clamp(_a, 0.f,255.f));
    }

    void SetColor(const uint8_t _r, const uint8_t _g, const uint8_t _b, const uint8_t _a = 0) {
        _color[0] = _r;
        _color[1] = _g;
        _color[2] = _b;
        _color[3] = _a;
    }

    void SetColor(const int _r, const int _g, const int _b, const int _a = 0) {
        _color[0] = _r;
        _color[1] = _g;
        _color[2] = _b;
        _color[3] = _a;
    }

    void GetColor(uint8_t &_r, uint8_t &_g, uint8_t &_b, uint8_t &_a) const {
        _r = _color[0];
        _g = _color[1];
        _b = _color[2];
        _a = _color[3];
    }

    void SetRawColor(const int color32) {
        *((int*) this) = color32;
    }

    int GetRawColor() const {
        return *((int*) this);
    }

    inline int r() const { return _color[0]; }
    inline int g() const { return _color[1]; }
    inline int b() const { return _color[2]; }
    inline int a() const { return _color[3]; }

    unsigned char& operator[](const int index) {
        return _color[index];
    }

    const unsigned char& operator[](const int index) const {
        return _color[index];
    }

    bool operator ==(const Color &rhs) const {
        return (*((int*) this) == *((int*) &rhs));
    }

    bool operator !=(const Color &rhs) const {
        return !(operator==(rhs));
    }

    Color& operator=(const Color &rhs) {
        SetRawColor(rhs.GetRawColor());
        return *this;
    }

    Color& operator=(const color32 &rhs) {
        _color[0] = rhs.r;
        _color[1] = rhs.g;
        _color[2] = rhs.b;
        _color[3] = rhs.a;
        return *this;
    }

    color32 ToColor32() const {
        color32 newColor;
        newColor.r = _color[0];
        newColor.g = _color[1];
        newColor.b = _color[2];
        newColor.a = _color[3];
        return newColor;
    }

private:
    uint8_t _color[4];
};
