// Photographs the front of Massif and every position of its parts: a path
// tracer over models of the parts (signed distance fields), lit as on a
// photographer's bench by one large softbox above and to the left. A
// development tool: its pictures are committed in Resources/photo, and the
// plugin only shows them.
//
//   massif_photo <folder> [quality] [names]
//
// quality 1 is the final render, 0.1 a quick look; names (comma separated)
// limits it to some of the pictures: front, knob, small, toggle; and
// "preview" lays the pictures in the folder out as the plugin will, every
// control part of the way round, into preview.png.
#include <juce_gui_basics/juce_gui_basics.h>

#include <PhotoData.h>

#include "../Source/dsp/Massif.h"
#include "../Source/ui/Layout.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace
{
using namespace onirique::layout;

constexpr float kPi = 3.14159265358979f;

//==============================================================================
struct V { float x = 0.0f, y = 0.0f, z = 0.0f; };

V operator+ (V a, V b)     { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
V operator- (V a, V b)     { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
V operator- (V a)          { return { -a.x, -a.y, -a.z }; }
V operator* (V a, float k) { return { a.x * k, a.y * k, a.z * k }; }
V operator* (V a, V b)     { return { a.x * b.x, a.y * b.y, a.z * b.z }; }
float dot (V a, V b)       { return a.x * b.x + a.y * b.y + a.z * b.z; }
float length (V a)         { return std::sqrt (dot (a, a)); }
V norm (V a)               { return a * (1.0f / length (a)); }
V cross (V a, V b)         { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
V grey (float v)           { return { v, v, v }; }
V mix (V a, V b, float t)  { return a * (1.0f - t) + b * t; }
float luminance (V c)      { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }
float clamp01 (float v)    { return std::fmin (1.0f, std::fmax (0.0f, v)); }

float smooth (float a, float b, float v)
{
    const auto t = clamp01 ((v - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

float wrap (float a)
{
    return a - 2.0f * kPi * std::floor ((a + kPi) / (2.0f * kPi));
}

/** Two directions square to n and to each other. */
void basis (V n, V& t, V& b)
{
    t = norm (cross (std::fabs (n.x) > 0.5f ? V { 0, 1, 0 } : V { 1, 0, 0 }, n));
    b = cross (n, t);
}

//==============================================================================
/** PCG32: a small, good random generator, one per pixel and sample. */
struct Rng
{
    explicit Rng (uint64_t seed) : state (seed * 6364136223846793005ull + 1442695040888963407ull) { next(); }

    float next()
    {
        const auto old = state;
        state = old * 6364136223846793005ull + 1442695040888963407ull;
        const auto shifted = (uint32_t) (((old >> 18u) ^ old) >> 27u);
        const auto rot = (uint32_t) (old >> 59u);
        const auto r = (shifted >> rot) | (shifted << ((32u - rot) & 31u));
        return (float) (r >> 8) * (1.0f / 16777216.0f);
    }

    uint64_t state;
};

/** Smooth value noise, for the grain of the paint and the wear of the plastic. */
float hash3 (int x, int y, int z)
{
    auto h = (uint32_t) (x * 374761393 + y * 668265263 + z * 1274126177);
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float) ((h ^ (h >> 16)) & 0xffffff) / 16777216.0f;
}

float noise (V p)
{
    const auto fx = std::floor (p.x), fy = std::floor (p.y), fz = std::floor (p.z);
    const int x = (int) fx, y = (int) fy, z = (int) fz;
    auto f = [] (float t) { return t * t * (3.0f - 2.0f * t); };
    const auto u = f (p.x - fx), v = f (p.y - fy), w = f (p.z - fz);
    auto lerp = [] (float a, float b, float t) { return a + (b - a) * t; };

    return lerp (lerp (lerp (hash3 (x, y, z), hash3 (x + 1, y, z), u), lerp (hash3 (x, y + 1, z), hash3 (x + 1, y + 1, z), u), v),
                 lerp (lerp (hash3 (x, y, z + 1), hash3 (x + 1, y, z + 1), u), lerp (hash3 (x, y + 1, z + 1), hash3 (x + 1, y + 1, z + 1), u), v), w);
}

float fbm (V p)
{
    return 0.5f * noise (p) + 0.25f * noise (p * 2.03f) + 0.125f * noise (p * 4.01f) + 0.125f * noise (p * 8.1f);
}

//==============================================================================
// Shapes as distances (after Inigo Quilez).
float smin (float a, float b, float k)
{
    const auto h = std::fmax (k - std::fabs (a - b), 0.0f) / k;
    return std::fmin (a, b) - h * h * k * 0.25f;
}

/** A cylinder standing on its axis (z), centred on the origin, its edges rounded. */
float roundCylinder (V p, float radius, float halfHeight, float round)
{
    const auto dx = std::sqrt (p.x * p.x + p.y * p.y) - radius + round, dy = std::fabs (p.z) - halfHeight + round;
    return std::fmin (std::fmax (dx, dy), 0.0f) + std::sqrt (std::fmax (dx, 0.0f) * std::fmax (dx, 0.0f) + std::fmax (dy, 0.0f) * std::fmax (dy, 0.0f)) - round;
}

float roundBox (V p, V half, float round)
{
    const V q { std::fabs (p.x) - half.x + round, std::fabs (p.y) - half.y + round, std::fabs (p.z) - half.z + round };
    const V out { std::fmax (q.x, 0.0f), std::fmax (q.y, 0.0f), std::fmax (q.z, 0.0f) };
    return length (out) + std::fmin (std::fmax (q.x, std::fmax (q.y, q.z)), 0.0f) - round;
}

float torus (V p, float major, float minor)
{
    const auto q = std::sqrt (p.x * p.x + p.y * p.y) - major;
    return std::sqrt (q * q + p.z * p.z) - minor;
}

/** A trapezium in two dimensions: r1 wide at the foot, r2 at the top, 2 he tall. */
float trapezoid (float px, float py, float r1, float r2, float he)
{
    const float k1x = r2, k1y = he, k2x = r2 - r1, k2y = 2.0f * he;
    px = std::fabs (px);
    const auto cax = px - std::fmin (px, py < 0.0f ? r1 : r2), cay = std::fabs (py) - he;
    const auto t = clamp01 (((k1x - px) * k2x + (k1y - py) * k2y) / (k2x * k2x + k2y * k2y));
    const auto cbx = px - k1x + k2x * t, cby = py - k1y + k2y * t;
    const auto s = (cbx < 0.0f && cay < 0.0f) ? -1.0f : 1.0f;
    return s * std::sqrt (std::fmin (cax * cax + cay * cay, cbx * cbx + cby * cby));
}

/** A cone with rounded ends, r1 at a, r2 at b. */
float roundCone (V p, V a, V b, float r1, float r2)
{
    const auto ba = b - a;
    const auto l2 = dot (ba, ba), rr = r1 - r2, a2 = l2 - rr * rr, il2 = 1.0f / l2;
    const auto pa = p - a;
    const auto y = dot (pa, ba), z = y - l2;
    const auto w = pa * l2 - ba * y;
    const auto x2 = dot (w, w), y2 = y * y * l2, z2 = z * z * l2;
    const auto k = (rr > 0.0f ? 1.0f : -1.0f) * rr * rr * x2;

    if ((z > 0.0f ? 1.0f : -1.0f) * a2 * z2 > k)
        return std::sqrt (x2 + z2) * il2 - r2;

    if ((y > 0.0f ? 1.0f : -1.0f) * a2 * y2 < k)
        return std::sqrt (x2 + y2) * il2 - r1;

    return (std::sqrt (x2 * a2 * il2) + y * rr) * il2 - r1;
}

//==============================================================================
enum class Material { panel, plate, window, bezel, bakelite, paint, chrome, lens, hollow };

/** A part standing on the front, in its own frame: its centre on the origin,
    the front at z = 0, z towards the camera, y down as on the screen. */
struct Part
{
    virtual ~Part() = default;
    virtual float distance (V p) const = 0;
    virtual Material material (V p) const = 0;

    V at;                                   // where it stands on the front
    float halfX = 0.0f, halfY = 0.0f;       // the box it fits in
    float top = 0.0f;
};

/** Where a point lies against a part turned by `angle`. */
struct Turned
{
    Turned (V p, float angle)
        : along (p.x * std::sin (angle) - p.y * std::cos (angle)),
          across (p.x * std::cos (angle) + p.y * std::sin (angle)),
          radius (std::sqrt (p.x * p.x + p.y * p.y)),
          theta (wrap (std::atan2 (p.x, -p.y) - angle))
    {}

    float along, across, radius, theta;
};

/** A flat shape in two dimensions stood up off the front, `height` tall, its edge rolled. */
float extrude (float flat, float z, float height, float round)
{
    const auto wx = flat + round, wy = std::fabs (z - 0.5f * height) - 0.5f * height + round;
    return std::fmin (std::fmax (wx, wy), 0.0f) + std::sqrt (std::fmax (wx, 0.0f) * std::fmax (wx, 0.0f) + std::fmax (wy, 0.0f) * std::fmax (wy, 0.0f)) - round;
}

float roundRect (float x, float y, float halfX, float halfY, float r)
{
    const auto qx = std::fabs (x) - halfX + r, qy = std::fabs (y) - halfY + r;
    return std::sqrt (std::fmax (qx, 0.0f) * std::fmax (qx, 0.0f) + std::fmax (qy, 0.0f) * std::fmax (qy, 0.0f))
           + std::fmin (std::fmax (qx, qy), 0.0f) - r;
}

/** The knob of the original, big or small: a black round body narrowing a
    little to its rolled top edge, a bar raised across the top from rim to
    rim, the pointer painted white along one end of the bar. */
struct Knob final : Part
{
    Knob (float radius, float a) : R (radius), angle (a) { halfX = halfY = R + 1.0f; top = 0.76f * R; }

    float distance (V p) const override
    {
        const Turned t (p, angle);
        const auto round = 0.1f * R;
        const auto body = trapezoid (t.radius, p.z - 0.28f * R, R - round, 0.92f * R - round, 0.28f * R - round) - round;
        const auto bar = std::fmax (roundBox ({ t.across, t.along, p.z - 0.62f * R }, { 0.19f * R, 0.95f * R, 0.1f * R }, 0.05f * R),
                                    t.radius - 0.9f * R);
        return smin (body, bar, 0.03f * R);
    }

    Material material (V p) const override
    {
        const Turned t (p, angle);
        return t.along > 0.42f * R && t.along < 0.86f * R && std::fabs (t.across) < 0.035f * R && p.z > 0.68f * R
                   ? Material::paint : Material::bakelite;
    }

    float R, angle;
};

/** The toggle: a knurled chrome washer and the domed chrome bushing on it,
    and the chrome bat standing out of it, tilted up or down by `tilt`, a
    ball at its end. */
struct Toggle final : Part
{
    explicit Toggle (float a) : tilt (a) { halfX = halfY = 0.8f * u; top = 1.25f * u; }

    static constexpr float u = kToggleUnit;

    V tip() const { return V { 0.0f, -std::sin (tilt), std::cos (tilt) } * (0.82f * u) + V { 0.0f, 0.0f, 0.2f * u }; }

    float handle (V p) const
    {
        const auto lever = roundCone (p, { 0, 0, 0.2f * u }, tip(), 0.1f * u, 0.065f * u);
        return smin (lever, length (p - tip()) - 0.14f * u, 0.03f * u);
    }

    float base (V p) const
    {
        const auto theta = std::atan2 (p.x, -p.y);
        const auto washer = roundCylinder (p - V { 0, 0, 0.04f * u }, 0.46f * u + 0.007f * u * std::cos (36.0f * theta), 0.04f * u, 0.012f * u);
        const auto bushing = std::fmax (length (p - V { 0, 0, -0.15f * u }) - 0.42f * u, -p.z);
        return std::fmin (washer, bushing);
    }

    float distance (V p) const override { return smin (base (p), handle (p), 0.02f * u); }
    Material material (V) const override { return Material::chrome; }

    float tilt;
};

/** An IN lamp: a domed lens in a black bezel. */
struct Lamp final : Part
{
    explicit Lamp (float x) { at = { x, kLampY, 0.0f }; halfX = halfY = kLampRadius + 1.0f; top = 5.5f; }

    float bezel (V p) const
    {
        const auto r = std::sqrt (p.x * p.x + p.y * p.y);
        return std::fmax (roundCylinder (p - V { 0, 0, 1.6f }, kLampRadius, 1.6f, 1.2f), 10.2f - r);
    }

    float lens (V p) const { return std::fmax (length (p - V { 0, 0, -9.0f }) - 13.8f, -p.z); }

    float distance (V p) const override { return std::fmin (bezel (p), lens (p)); }
    Material material (V p) const override { return lens (p) < bezel (p) ? Material::lens : Material::bakelite; }
};

/** A black oval plate a band sits on: a rounded slot, its ends half circles, standing off the front. */
struct PlatePart final : Part
{
    explicit PlatePart (const Plate& p)
        : w (p.halfWidth), straight (0.5f * (p.bottom - p.top) - p.halfWidth)
    {
        at = { p.x, 0.5f * (p.top + p.bottom), 0.0f };
        halfX = w + 1.0f;
        halfY = straight + w + 1.0f;
        top = kPlateHeight + 0.1f;
    }

    float distance (V p) const override
    {
        const auto dy = std::fmax (std::fabs (p.y) - straight, 0.0f);
        return extrude (std::sqrt (p.x * p.x + dy * dy) - w, p.z, kPlateHeight, 0.8f);
    }

    Material material (V) const override { return Material::plate; }

    float w, straight;
};

/** A band's window: a black face sunk in a bezel of the front's own paint. */
struct WindowPart final : Part
{
    static constexpr float rim = 3.5f, floor = 1.2f;

    explicit WindowPart (const Window& w) : hx (w.halfWidth), hy (w.halfHeight)
    {
        at = { w.x, w.y, 0.0f };
        halfX = hx + rim + 1.0f;
        halfY = hy + rim + 1.0f;
        top = kWindowHeight + 0.1f;
    }

    float inner (V p) const { return roundRect (p.x, p.y, hx, hy, kWindowRadius); }

    float distance (V p) const override
    {
        const auto outer = extrude (roundRect (p.x, p.y, hx + rim, hy + rim, kWindowRadius + rim), p.z, kWindowHeight, 1.2f);
        return std::fmax (outer, -std::fmax (inner (p) + 0.4f, floor - p.z));
    }

    Material material (V p) const override { return inner (p) < 0.05f && p.z < floor + 0.1f ? Material::window : Material::bezel; }

    float hx, hy;
};

/** A slotted, domed chrome screw. */
struct ScrewPart final : Part
{
    explicit ScrewPart (const Screw& s) : r (s.radius), slot (s.slot) { at = { s.x, s.y, 0.0f }; halfX = halfY = r + 1.0f; top = 0.4f * r; }

    float distance (V p) const override
    {
        auto head = std::fmax (roundCylinder (p - V { 0, 0, 0.155f * r }, r, 0.155f * r, 0.136f * r), length (p - V { 0, 0, -1.82f * r }) - 2.11f * r);
        const auto along = p.x * std::cos (slot) + p.y * std::sin (slot), across = -p.x * std::sin (slot) + p.y * std::cos (slot);
        return std::fmax (head, -roundBox ({ along, across, p.z - 0.33f * r }, { 1.14f * r, 0.1f * r, 0.22f * r }, 0.03f * r));
    }

    Material material (V) const override { return Material::chrome; }

    float r, slot;
};

//==============================================================================
/** What the camera sees at a point: how it reflects, and what it gives off. */
struct Surface
{
    V albedo, f0;
    float rough = 0.5f;
    bool metal = false;
    V emission;
};

/** The lettering, as masks the front is printed with: white, and the words
    that light green and red in the band windows. */
struct Ink
{
    std::vector<float> white, boost, cut;
    int width = 0, height = 0;
};

struct Scene
{
    std::vector<const Part*> parts;
    std::vector<const Part*> lamps;     // the IN lamps, also among the parts
    bool slots = false;                 // the rack ears cut through the front
    const Ink* ink = nullptr;
    bool lit = false;                   // the lamps and the words of the windows alight
    Material floor = Material::panel;   // what the front is where nothing stands on it
};

struct Hit
{
    float t = 1.0e30f;
    V p, n;
    Material material = Material::panel;
    const Part* part = nullptr;
};

bool inSlot (float x, float y)
{
    for (const auto& s : kSlots)
    {
        const auto r = 0.5f * kSlotHeight;
        const auto dx = std::fmax (std::fabs (x - s.x) - (0.5f * kSlotLength - r), 0.0f), dy = y - s.y;

        if (dx * dx + dy * dy < r * r)
            return true;
    }

    return false;
}

/** The ray's way into a part's box: from t0 to t1, or nothing. */
bool enter (const Part& part, V o, V d, float& t0, float& t1)
{
    t0 = 0.0f;
    t1 = 1.0e30f;

    auto slab = [&] (float origin, float direction, float lo, float hi)
    {
        if (std::fabs (direction) < 1.0e-8f)
            return origin >= lo && origin <= hi;

        auto a = (lo - origin) / direction, b = (hi - origin) / direction;

        if (a > b)
            std::swap (a, b);

        t0 = std::fmax (t0, a);
        t1 = std::fmin (t1, b);
        return t0 < t1;
    };

    return slab (o.x, d.x, part.at.x - part.halfX, part.at.x + part.halfX)
        && slab (o.y, d.y, part.at.y - part.halfY, part.at.y + part.halfY)
        && slab (o.z, d.z, 0.0f, part.top);
}

/** The nearest thing along a ray, short of `limit`; `skip` is a part the ray passes through. */
bool intersect (const Scene& scene, V o, V d, Hit& hit, float limit = 1.0e30f, const Part* skip = nullptr)
{
    hit.t = limit;
    bool found = false;

    // The front: a plane, cut through at the rack ears, the rack's dark inside behind.
    if (d.z < -1.0e-6f)
    {
        const auto t = -o.z / d.z;

        if (t > 1.0e-4f && t < hit.t)
        {
            const auto p = o + d * t;

            if (scene.slots && inSlot (p.x, p.y))
            {
                const auto behind = (-8.0f - o.z) / d.z;

                if (behind < hit.t)
                {
                    hit = { behind, o + d * behind, { 0, 0, 1 }, Material::hollow, nullptr };
                    found = true;
                }
            }
            else
            {
                hit = { t, p, { 0, 0, 1 }, scene.floor, nullptr };
                found = true;
            }
        }
    }

    for (const auto* part : scene.parts)
    {
        float t0, t1;

        if (part == skip || ! enter (*part, o, d, t0, t1) || t0 > hit.t)
            continue;

        auto t = t0;

        for (int i = 0; i < 300 && t < t1 && t < hit.t; ++i)
        {
            const auto distance = part->distance (o + d * t - part->at);

            if (distance < 0.002f)
            {
                hit.t = t;
                hit.part = part;
                found = true;
                break;
            }

            // The knurls bend the distances a little: step short of them.
            t += std::fmax (distance * 0.7f, 0.001f);
        }
    }

    if (found && hit.part != nullptr)
    {
        const auto* part = hit.part;
        const auto local = o + d * hit.t - part->at;
        constexpr float e = 0.004f;
        auto f = [&] (V q) { return part->distance (q); };
        hit.p = o + d * hit.t;
        hit.n = norm (V { 1, -1, -1 } * f (local + V { e, -e, -e }) + V { -1, -1, 1 } * f (local + V { -e, -e, e })
                      + V { -1, 1, -1 } * f (local + V { -e, e, -e }) + V { 1, 1, 1 } * f (local + V { e, e, e }));
        hit.material = part->material (local);
    }

    return found;
}

//==============================================================================
// The light, as for the photographs of the original: a large softbox
// overhead, which glossy faces tilted up mirror as grey; a smaller, brighter
// one above and to the left for the highlights and the shadows; a strip
// light along the top, a white card on the right, a studio round them, and
// the lamps when they are lit.
struct ConeLight { V direction; float cosMax; V radiance; };

const ConeLight kLights[] = {
    { norm ({ -0.12f, -0.55f, 0.83f }), std::cos (24.0f * kPi / 180.0f), V { 4.6f, 4.6f, 4.5f } },
    { norm ({ -0.45f, -0.6f, 0.66f }), std::cos (9.0f * kPi / 180.0f), V { 26.0f, 25.5f, 24.5f } },
    { norm ({ 0.05f, -0.8f, 0.6f }), std::cos (6.0f * kPi / 180.0f), V { 9.0f, 9.0f, 9.2f } },
    { norm ({ 0.62f, -0.25f, 0.74f }), std::cos (24.0f * kPi / 180.0f), V { 1.0f, 1.03f, 1.08f } },
};

constexpr float kLampGlowRadius = 5.0f;
const V kLampGlow { 0.25f, 1.0f, 3.2f };

V studio (V d)
{
    const auto up = -d.y;
    auto v = 0.05f + 0.14f * smooth (-0.3f, 0.8f, up) * smooth (-0.2f, 0.4f, d.z);

    // White cards to the left and below, catching the softbox: what chrome
    // and gloss show beside their highlights.
    if (d.x < -0.5f && d.z > 0.1f && d.y < 0.5f)
        v += 0.55f;

    if (d.y > 0.55f && d.z > 0.2f)
        v += 0.12f;

    return { v * 1.02f, v, v * 0.97f };
}

const V kPanelColour { 0.125f, 0.0145f, 0.017f };   // a red where the original is violet
const V kInkColour { 0.78f, 0.77f, 0.75f };
const V kBoostGlow { 0.12f, 1.7f, 0.14f }, kCutGlow { 2.0f, 0.09f, 0.07f };

float inkAt (const Ink* ink, const std::vector<float> Ink::* mask, float x, float y)
{
    if (ink == nullptr)
        return 0.0f;

    const auto& m = ink->*mask;
    const auto fx = x * (float) kPhotoScale - 0.5f, fy = y * (float) kPhotoScale - 0.5f;
    const auto ix = (int) std::floor (fx), iy = (int) std::floor (fy);
    auto at = [&] (int i, int j)
    {
        return i < 0 || j < 0 || i >= ink->width || j >= ink->height ? 0.0f : m[(size_t) (j * ink->width + i)];
    };
    const auto u = fx - (float) ix, v = fy - (float) iy;
    return (at (ix, iy) * (1 - u) + at (ix + 1, iy) * u) * (1 - v) + (at (ix, iy + 1) * (1 - u) + at (ix + 1, iy + 1) * u) * v;
}

Surface surfaceAt (const Scene& scene, Hit& hit)
{
    Surface s;
    const auto p = hit.p;
    const auto white = hit.part == nullptr || hit.material == Material::plate || hit.material == Material::window
                           ? inkAt (scene.ink, &Ink::white, p.x, p.y) : 0.0f;

    switch (hit.material)
    {
        case Material::panel:
        {
            // Satin paint over a front brushed side to side: fine streaks run
            // along it, catching the light a little differently each.
            const auto drift = 1.0f + 0.07f * (fbm (p * 0.02f) - 0.5f);
            const auto streak = [] (float x, float y) { return noise ({ x * 0.012f, y * 3.1f, 0.0f }) + 0.5f * noise ({ x * 0.03f, y * 7.3f, 4.0f }); };
            constexpr float e = 0.08f;
            const auto gy = streak (p.x, p.y + e) - streak (p.x, p.y - e);
            hit.n = norm ({ 0.0f, -gy * 0.05f, 1.0f });

            const auto grain = 0.93f + 0.1f * streak (p.x, p.y) / 1.5f;
            s.albedo = mix (kPanelColour * (drift * grain), kInkColour, white);
            s.f0 = grey (0.04f);
            s.rough = 0.42f - 0.1f * white;
            break;
        }

        case Material::bezel:
            s.albedo = kPanelColour;
            s.f0 = grey (0.05f);
            s.rough = 0.2f;
            break;

        case Material::plate:
            s.albedo = mix (grey (0.01f), kInkColour, white);
            s.f0 = grey (0.04f);
            s.rough = 0.16f + 0.3f * white;
            break;

        case Material::window:
        {
            const auto boost = inkAt (scene.ink, &Ink::boost, p.x, p.y), cut = inkAt (scene.ink, &Ink::cut, p.x, p.y);

            // Unlit, the words that light are printed white like the rest.
            s.albedo = mix (grey (0.008f), kInkColour, scene.lit ? white : std::fmin (1.0f, white + boost + cut));
            s.f0 = grey (0.045f);
            s.rough = 0.12f;

            if (scene.lit)
                s.emission = kBoostGlow * boost + kCutGlow * cut;

            break;
        }

        case Material::bakelite:
            s.albedo = { 0.012f, 0.011f, 0.0105f };
            s.f0 = grey (0.045f);
            s.rough = 0.08f + 0.03f * noise (p * 0.35f);
            break;

        case Material::paint:
            s.albedo = { 0.8f, 0.79f, 0.76f };
            s.f0 = grey (0.04f);
            s.rough = 0.4f;
            break;

        case Material::chrome:
            s.metal = true;
            s.f0 = { 0.62f, 0.63f, 0.65f };
            s.rough = 0.08f;
            break;

        case Material::lens:
        {
            s.albedo = { 0.01f, 0.02f, 0.05f };
            s.f0 = grey (0.05f);
            s.rough = 0.18f;

            // The bulb behind the frosted dome: brightest at the middle.
            const auto core = clamp01 (1.0f - length (V { p.x - hit.part->at.x, p.y - hit.part->at.y, 0 }) / 10.5f);
            s.emission = scene.lit ? V { 0.03f, 0.16f, 1.0f } * (1.3f + 4.0f * core * core) + V { 0.3f, 0.55f, 1.0f } * (1.6f * std::pow (core, 5.0f))
                                   : V { 0.003f, 0.007f, 0.018f };
            break;
        }

        case Material::hollow:
            s.albedo = grey (0.006f);
            s.f0 = grey (0.02f);
            s.rough = 0.9f;
            break;
    }

    return s;
}

//==============================================================================
// Reflection: Lambert under GGX microfacets, Schlick's Fresnel.
V schlick (V f0, float c)
{
    const auto m = std::pow (1.0f - clamp01 (c), 5.0f);
    return f0 + (grey (1.0f) - f0) * m;
}

float ggx (float nh, float a)
{
    const auto a2 = a * a, d = nh * nh * (a2 - 1.0f) + 1.0f;
    return a2 / (kPi * d * d);
}

float smith (float nv, float a)
{
    return 2.0f * nv / (nv + std::sqrt (a * a + (1.0f - a * a) * nv * nv));
}

float specularChance (const Surface& s, float nv)
{
    if (s.metal)
        return 1.0f;

    const auto fresnel = luminance (schlick (s.f0, nv)), diffuse = luminance (s.albedo) * (1.0f - fresnel);
    return std::fmin (0.95f, std::fmax (0.15f, fresnel / (fresnel + diffuse)));
}

V evaluate (const Surface& s, V n, V wo, V wi)
{
    const auto nl = dot (n, wi), nv = dot (n, wo);

    if (nl <= 0.0f || nv <= 0.0f)
        return {};

    const auto h = norm (wo + wi);
    const auto a = std::fmax (0.002f, s.rough * s.rough);
    const auto f = schlick (s.f0, dot (wi, h));
    const auto spec = f * (ggx (dot (n, h), a) * smith (nl, a) * smith (nv, a) / (4.0f * nl * nv));

    if (s.metal)
        return spec;

    return spec + s.albedo * (1.0f - luminance (schlick (s.f0, nv))) * (1.0f / kPi);
}

float density (const Surface& s, V n, V wo, V wi)
{
    const auto nl = dot (n, wi);

    if (nl <= 0.0f)
        return 0.0f;

    const auto h = norm (wo + wi);
    const auto a = std::fmax (0.002f, s.rough * s.rough);
    const auto chance = specularChance (s, dot (n, wo));
    return chance * ggx (dot (n, h), a) * dot (n, h) / (4.0f * std::fmax (1.0e-6f, dot (wo, h))) + (1.0f - chance) * nl / kPi;
}

V sampleDirection (const Surface& s, V n, V wo, Rng& rng)
{
    V t, b;
    basis (n, t, b);
    const auto u1 = rng.next(), u2 = rng.next();

    if (rng.next() < specularChance (s, dot (n, wo)))
    {
        const auto a = std::fmax (0.002f, s.rough * s.rough);
        const auto tan2 = a * a * u1 / (1.0f - u1 + 1.0e-7f);
        const auto cosT = 1.0f / std::sqrt (1.0f + tan2), sinT = std::sqrt (std::fmax (0.0f, 1.0f - cosT * cosT));
        const auto phi = 2.0f * kPi * u2;
        const auto h = t * (sinT * std::cos (phi)) + b * (sinT * std::sin (phi)) + n * cosT;
        return h * (2.0f * dot (wo, h)) - wo;
    }

    const auto r = std::sqrt (u1), phi = 2.0f * kPi * u2;
    return t * (r * std::cos (phi)) + b * (r * std::sin (phi)) + n * std::sqrt (std::fmax (0.0f, 1.0f - u1));
}

/** A direction in a cone round `axis`, uniformly. */
V sampleCone (V axis, float cosMax, Rng& rng)
{
    V t, b;
    basis (axis, t, b);
    const auto c = 1.0f - rng.next() * (1.0f - cosMax), s = std::sqrt (std::fmax (0.0f, 1.0f - c * c)), phi = 2.0f * kPi * rng.next();
    return t * (s * std::cos (phi)) + b * (s * std::sin (phi)) + axis * c;
}

float coneDensity (float cosMax) { return 1.0f / (2.0f * kPi * (1.0f - cosMax)); }

float balance (float a, float b) { return a * a / (a * a + b * b); }

/** The light a ray brings back. The paths are the same whether the lamps are
    lit or not, so the two photographs of the front differ only where the
    light does, and the plugin can lay one over the other in places. */
V trace (const Scene& scene, V o, V d, Rng& rng)
{
    V radiance, through = grey (1.0f);
    float lastDensity = 0.0f;

    for (int depth = 0; depth < 4; ++depth)
    {
        Hit hit;

        if (! intersect (scene, o, d, hit))
        {
            auto sky = studio (d);

            // The lights themselves, as a ray bounced off a surface finds them.
            for (const auto& light : kLights)
                if (dot (d, light.direction) >= light.cosMax)
                    sky = sky + light.radiance * (depth == 0 ? 1.0f : balance (lastDensity, coneDensity (light.cosMax)));

            radiance = radiance + through * sky;
            break;
        }

        const auto s = surfaceAt (scene, hit);
        auto n = hit.n;
        const auto wo = -d;

        if (dot (n, wo) < 0.0f)
            n = -n;

        // What a glowing surface gives off is seen straight on; further down a
        // path, the lamps are counted as lights instead.
        if (depth == 0)
            radiance = radiance + through * s.emission;

        const auto from = hit.p + n * 0.01f;

        for (const auto& light : kLights)
        {
            const auto wi = sampleCone (light.direction, light.cosMax, rng);
            Hit blocker;

            if (dot (n, wi) <= 0.0f || intersect (scene, from, wi, blocker))
                continue;

            const auto lightDensity = coneDensity (light.cosMax);
            radiance = radiance + through * evaluate (s, n, wo, wi) * light.radiance
                                    * (dot (n, wi) / lightDensity * balance (lightDensity, density (s, n, wo, wi)));
        }

        // The lamps' glow on the front round them; drawn for, lit or not, so the paths stay the same.
        for (const auto* lamp : scene.lamps)
        {
            const auto toLamp = lamp->at + V { 0, 0, 3.0f } - hit.p;
            const auto distance = length (toLamp);

            if (distance <= kLampGlowRadius || hit.part != nullptr)
                continue;

            const auto sinMax = kLampGlowRadius / distance, cosMax = std::sqrt (1.0f - sinMax * sinMax);
            const auto wi = sampleCone (toLamp * (1.0f / distance), cosMax, rng);
            Hit blocker;

            if (scene.lit && dot (n, wi) > 0.0f && ! intersect (scene, from, wi, blocker, distance, lamp))
                radiance = radiance + through * evaluate (s, n, wo, wi) * kLampGlow * (dot (n, wi) / coneDensity (cosMax));
        }

        const auto wi = sampleDirection (s, n, wo, rng);
        const auto pdf = density (s, n, wo, wi);

        if (pdf <= 1.0e-6f || dot (n, wi) <= 0.0f)
            break;

        through = through * evaluate (s, n, wo, wi) * (dot (n, wi) / pdf);

        // A rare path that finds the light through a tight gloss would leave a white speck.
        if (luminance (through) > 4.0f)
            through = through * (4.0f / luminance (through));

        lastDensity = pdf;
        o = from;
        d = wi;
    }

    return radiance;
}

//==============================================================================
/** From light to the screen: a filmic curve (ACES, after K. Narkowicz), then sRGB. */
constexpr float kExposure = 1.0f;

float toScreen (float x)
{
    x *= kExposure;
    const auto film = clamp01 ((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f));
    return film <= 0.0031308f ? 12.92f * film : 1.055f * std::pow (film, 1.0f / 2.4f) - 0.055f;
}

/** Runs rows (from, to) over every core. */
template <typename Rows>
void everyCore (int count, Rows rows)
{
    std::atomic<int> next { 0 };
    std::vector<std::thread> threads;

    for (unsigned c = 0; c < std::max (1u, std::thread::hardware_concurrency()); ++c)
        threads.emplace_back ([&]
        {
            for (int row; (row = next++) < count;)
                rows (row);
        });

    for (auto& t : threads)
        t.join();
}

uint64_t seedOf (int x, int y, int sample, int salt)
{
    return ((uint64_t) (uint32_t) x * 73856093u) ^ ((uint64_t) (uint32_t) y * 19349663u << 20) ^ ((uint64_t) (uint32_t) sample << 40) ^ (uint64_t) salt * 0x9e3779b97f4a7c15ull;
}

//==============================================================================
/** The lettering of the front: what is printed white on the paint, the plates
    and the windows, and apart the words of the windows that light. */
Ink printLettering()
{
    using namespace juce;
    const int w = kWidth * kPhotoScale, h = kHeight * kPhotoScale;
    Image white (Image::ARGB, w, h, true, SoftwareImageType()), boostMask = white.createCopy(), cutMask = white.createCopy();

    {
        Graphics g (white), gb (boostMask), gc (cutMask);

        for (auto* each : { &g, &gb, &gc })
        {
            each->addTransform (AffineTransform::scale ((float) kPhotoScale));
            each->setColour (Colours::white);
        }

        const auto face = Typeface::createSystemTypefaceFor (PhotoData::HankenGrotesk600_ttf, PhotoData::HankenGrotesk600_ttfSize);
        auto fontOf = [&] (float height, float tracking) { return Font (FontOptions (face).withHeight (height)).withExtraKerningFactor (tracking); };

        auto text = [&] (Graphics& on, const String& s, float x, float y, float height, float tracking = 0.02f)
        {
            on.setFont (fontOf (height, tracking));
            on.drawText (s, Rectangle<float> (240.0f, height * 1.6f).withCentre ({ x, y }), Justification::centred, false);
        };

        auto around = [] (float cx, float cy, float degrees, float radius)
        {
            const auto a = degrees * kPi / 180.0f;
            return Point<float> (cx + radius * std::sin (a), cy - radius * std::cos (a));
        };

        auto tick = [&] (float cx, float cy, float degrees, float from, float to, float thickness)
        {
            g.drawLine (Line<float> (around (cx, cy, degrees, from), around (cx, cy, degrees, to)), thickness);
        };

        auto arc = [&] (float cx, float cy, float radius, float from, float to, float thickness)
        {
            Path p;
            p.addCentredArc (cx, cy, radius, radius, 0.0f, from * kPi / 180.0f, to * kPi / 180.0f, true);
            g.strokePath (p, PathStrokeType (thickness, PathStrokeType::curved, PathStrokeType::rounded));
        };

        /** Words round a knob, each pushed out by half its own width where it lies to the side. */
        auto ring = [&] (float cx, float cy, float degrees, float radius, const String& s, float height)
        {
            const auto width = GlyphArrangement::getStringWidth (fontOf (height, 0.0f), s);
            const auto at = around (cx, cy, degrees, radius + 0.5f * width * std::fabs (std::sin (degrees * kPi / 180.0f)));
            text (g, s, at.x, at.y, height, 0.0f);
        };

        // The bands.
        for (int side = 0; side < 2; ++side)
            for (int band = 0; band < 4; ++band)
            {
                const auto x = kBandX[side][band];

                // The window: the band's range, and its two switches.
                text (g, msf::kBandRanges[band], x, 20.4f, 11.5f, 0.04f);
                text (gb, "BOOST", x + kBoostLight.dx, kBoostLight.y, 11.5f);
                text (g, "SHELF", x + 27.0f, 35.0f, 11.5f);
                text (g, String::fromUTF8 ("–OUT"), x + 1.0f, kToggleY, 11.0f);
                text (gc, "CUT", x + kCutLight.dx, kCutLight.y, 11.5f);
                text (g, "BELL", x + 27.0f, 73.6f, 11.5f);

                // The gain: a scale from 0 to 20 dB, its ends drawn out.
                arc (x, kGainY, 35.5f, -140.0f, 140.0f, 1.1f);

                for (int i = 0; i <= 10; ++i)
                {
                    const auto end = i == 0 || i == 10;
                    tick (x, kGainY, -140.0f + 28.0f * (float) i, 35.5f, end ? 42.0f : 39.5f, end ? 1.6f : 1.1f);
                }

                text (g, "0", x - 32.0f, 186.5f, 11.0f);
                text (g, "dB", x, 187.5f, 11.0f);
                text (g, "20", x + 31.0f, 186.5f, 11.0f);
                text (g, "BANDWIDTH", x, 198.5f, 10.8f);

                // The bandwidth, and what it does: wide or narrow, gentle shelf or resonant.
                arc (x, kBandwidthY, 28.5f, -135.0f, 135.0f, 1.0f);

                for (int i = 0; i <= 6; ++i)
                    tick (x, kBandwidthY, -135.0f + 45.0f * (float) i, 28.5f, 31.5f, 1.0f);

                {
                    const auto lx = x - 41.0f, rx = x + 43.0f, archY = kBandwidthY - 7.0f, shelfY = kBandwidthY + 12.0f;
                    const PathStrokeType stroke (1.2f, PathStrokeType::curved, PathStrokeType::rounded);
                    Path p;

                    p.startNewSubPath (lx - 9.0f, archY + 5.0f);
                    p.cubicTo (lx - 8.0f, archY - 6.0f, lx + 8.0f, archY - 6.0f, lx + 9.0f, archY + 5.0f);
                    p.startNewSubPath (rx - 5.0f, archY + 5.0f);
                    p.cubicTo (rx - 2.5f, archY - 8.0f, rx + 2.5f, archY - 8.0f, rx + 5.0f, archY + 5.0f);

                    p.startNewSubPath (lx - 10.0f, shelfY);
                    p.lineTo (lx - 2.0f, shelfY);
                    p.quadraticTo (lx + 4.0f, shelfY, lx + 9.0f, shelfY - 6.0f);
                    p.startNewSubPath (lx - 2.0f, shelfY);
                    p.quadraticTo (lx + 4.0f, shelfY, lx + 9.0f, shelfY + 6.0f);

                    p.startNewSubPath (rx - 10.0f, shelfY - 6.0f);
                    p.quadraticTo (rx - 6.0f, shelfY, rx - 1.0f, shelfY);
                    p.startNewSubPath (rx - 10.0f, shelfY + 6.0f);
                    p.quadraticTo (rx - 6.0f, shelfY, rx - 1.0f, shelfY);
                    p.startNewSubPath (rx + 3.0f, shelfY);
                    p.lineTo (rx + 10.0f, shelfY);
                    g.strokePath (p, stroke);
                    g.drawEllipse (Rectangle<float> (4.0f, 4.0f).withCentre ({ rx + 1.0f, shelfY }), 1.1f);
                }

                text (g, "FREQUENCY HZ", x, 273.7f, 10.8f);

                // The frequency switch: its eleven positions.
                for (int i = 0; i < msf::kNumSteps; ++i)
                {
                    const auto degrees = -155.0f + 31.0f * (float) i;
                    tick (x, kFreqY, degrees, 30.5f, 34.0f, 1.1f);
                    ring (x, kFreqY, degrees, 41.5f, msf::kBandFreqNames[band][i], 10.5f);
                }
            }

        // The middle: the maker's name, drawn in outline like an engraving.
        {
            Font serif (FontOptions ("Times New Roman", 33.0f, Font::plain));
            GlyphArrangement glyphs;
            glyphs.addLineOfText (serif.withExtraKerningFactor (0.16f), "ONIRIQUE", 0.0f, 0.0f);
            Path logo;
            glyphs.createPath (logo);
            const auto b = logo.getBounds();
            logo.applyTransform (AffineTransform::translation (kCentreX - b.getCentreX(), 34.0f - b.getCentreY()));
            g.strokePath (logo, PathStrokeType (0.9f));
        }

        text (g, "POWER", kCentreX, 65.0f, 11.0f);
        text (g, "GAIN", kCentreX, 130.5f, 11.0f);
        text (g, "LINK", kCentreX, 161.0f, 11.0f);
        text (g, "LOW", kCentreX, 208.6f, 10.5f);
        text (g, "PASS", kCentreX, 218.0f, 10.5f);
        text (g, "HIGH", kCentreX, 293.4f, 10.5f);
        text (g, "PASS", kCentreX, 302.8f, 10.5f);
        // The name; under it, the licence badge's place (kBadge).
        text (g, "MASSIF", kCentreX, 351.0f, 12.5f, 0.14f);

        for (int side = 0; side < 2; ++side)
        {
            const auto x = kMiddleX[side];
            text (g, "IN", kLampX[side], 77.0f, 11.0f);

            // The gain: a tick for each dB, -6 to the left, +4 to the right.
            for (int db = -6; db <= 4; ++db)
                tick (x, kOutGainY, db < 0 ? 145.0f * (float) db / 6.0f : 145.0f * (float) db / 4.0f, 23.5f, 28.0f, 1.1f);

            text (g, "0", x, 121.0f, 11.0f);
            ring (x, kOutGainY, -150.0f, 35.0f, "-6", 11.0f);
            ring (x, kOutGainY, 150.0f, 35.0f, "+4", 11.0f);

            for (int i = 0; i < msf::kNumFilters; ++i)
            {
                const auto degrees = -77.0f + 30.8f * (float) i;
                tick (x, kLowPassY, degrees, 23.5f, 27.5f, 1.1f);
                ring (x, kLowPassY, degrees, 34.0f, msf::kLowPassNames[i], 10.5f);
                tick (x, kHighPassY, degrees, 23.5f, 27.5f, 1.1f);
                ring (x, kHighPassY, degrees, 34.0f, msf::kHighPassNames[i], 10.5f);
            }
        }
    }

    Ink ink;
    ink.width = w;
    ink.height = h;

    for (auto [image, mask] : { std::pair (&white, &ink.white), std::pair (&boostMask, &ink.boost), std::pair (&cutMask, &ink.cut) })
    {
        mask->resize ((size_t) (w * h));
        const Image::BitmapData data (*image, Image::BitmapData::readOnly);

        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                (*mask)[(size_t) (y * w + x)] = (float) data.getPixelColour (x, y).getAlpha() / 255.0f;
    }

    return ink;
}

/** The whole front, with its plates, windows, screws and lamps, lit or not. */
juce::Image photographFront (bool lit, int samples, const Ink& ink)
{
    std::vector<std::unique_ptr<Part>> owned;
    Scene scene;
    scene.slots = true;
    scene.ink = &ink;
    scene.lit = lit;

    for (const auto& p : plates())
        owned.push_back (std::make_unique<PlatePart> (p));

    for (const auto& w : windows())
        owned.push_back (std::make_unique<WindowPart> (w));

    for (const auto& s : screws())
        owned.push_back (std::make_unique<ScrewPart> (s));

    for (auto x : kLampX)
    {
        owned.push_back (std::make_unique<Lamp> (x));
        scene.lamps.push_back (owned.back().get());
    }

    for (const auto& p : owned)
        scene.parts.push_back (p.get());

    const int w = ink.width, h = ink.height;
    juce::Image image (juce::Image::RGB, w, h, false, juce::SoftwareImageType());
    juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
    const auto side = (int) std::ceil (std::sqrt ((float) samples));

    everyCore (h, [&] (int y)
    {
        for (int x = 0; x < w; ++x)
        {
            V sum;

            for (int s = 0; s < side * side; ++s)
            {
                Rng rng (seedOf (x, y, s, 1));
                const auto px = ((float) x + ((float) (s % side) + rng.next()) / (float) side) / (float) kPhotoScale;
                const auto py = ((float) y + ((float) (s / side) + rng.next()) / (float) side) / (float) kPhotoScale;
                sum = sum + trace (scene, { px, py, 200.0f }, { 0, 0, -1 }, rng);
            }

            const auto c = sum * (1.0f / (float) (side * side));
            data.setPixelColour (x, y, juce::Colour::fromFloatRGBA (toScreen (c.x), toScreen (c.y), toScreen (c.z), 1.0f));
        }
    });

    return image;
}

std::unique_ptr<Part> makePart (Kind kind, float angle)
{
    switch (kind)
    {
        case Kind::knob:   return std::make_unique<Knob> (kKnobRadius, angle);
        case Kind::small:  return std::make_unique<Knob> (kSmallRadius, angle);
        case Kind::toggle: return std::make_unique<Toggle> (angle);
    }

    return {};
}

/** Every position of a part, on a plain front: the part itself, and the
    shadow it casts as a darkening of whatever lies under it. Laid out in a
    grid, eight to a row. */
juce::Image photographRun (const Run& run, int samples)
{
    const auto frame = run.frame;
    const auto pixels = (int) std::lround (frame.size() * (float) kPhotoScale);
    const auto columns = std::min (run.frames, 8), rows = (run.frames + columns - 1) / columns;

    juce::Image image (juce::Image::ARGB, columns * pixels, rows * pixels, true, juce::SoftwareImageType());
    juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
    const auto side = (int) std::ceil (std::sqrt ((float) samples));
    const auto floor = run.floor == Floor::plate ? Material::plate : run.floor == Floor::window ? Material::window : Material::panel;
    Scene bare;
    bare.floor = floor;

    for (int k = 0; k < run.frames; ++k)
    {
        const auto degrees = run.fromDegrees + (run.toDegrees - run.fromDegrees) * (float) k / (float) (run.frames - 1);
        const auto part = makePart (run.kind, degrees * kPi / 180.0f);
        Scene scene;
        scene.floor = floor;
        scene.parts.push_back (part.get());

        const auto left = (k % columns) * pixels, top = (k / columns) * pixels;

        everyCore (pixels, [&] (int y)
        {
            for (int x = 0; x < pixels; ++x)
            {
                V onPart, withIt, without;
                int hits = 0, misses = 0;

                for (int s = 0; s < side * side; ++s)
                {
                    Rng rng (seedOf (x, y, s, 100 + k));
                    const auto px = -frame.before + ((float) x + ((float) (s % side) + rng.next()) / (float) side) / (float) kPhotoScale;
                    const auto py = -frame.before + ((float) y + ((float) (s / side) + rng.next()) / (float) side) / (float) kPhotoScale;
                    const V o { px, py, 200.0f }, d { 0, 0, -1 };
                    Hit first;

                    if (intersect (scene, o, d, first) && first.part != nullptr)
                    {
                        onPart = onPart + trace (scene, o, d, rng);
                        ++hits;
                    }
                    else
                    {
                        // The same path with the part and without it: their ratio is the shadow.
                        Rng twin = rng;
                        withIt = withIt + trace (scene, o, d, rng);
                        without = without + trace (bare, o, d, twin);
                        ++misses;
                    }
                }

                const auto cover = (float) hits / (float) (side * side);
                V colour;

                if (hits > 0)
                {
                    const auto c = onPart * (1.0f / (float) hits);
                    colour = { toScreen (c.x), toScreen (c.y), toScreen (c.z) };
                }

                auto shadow = 0.0f;

                if (misses > 0)
                {
                    const auto w = withIt * (1.0f / (float) misses), wo = without * (1.0f / (float) misses);
                    const auto shown = luminance ({ toScreen (w.x), toScreen (w.y), toScreen (w.z) });
                    const auto bright = luminance ({ toScreen (wo.x), toScreen (wo.y), toScreen (wo.z) });
                    // On a black floor there is nothing for a shadow to darken.
                    shadow = bright > 0.03f ? clamp01 (1.0f - shown / bright) : 0.0f;
                }

                const auto alpha = cover + (1.0f - cover) * shadow;
                auto byte = [] (float v) { return (juce::uint8) std::lround (255.0f * clamp01 (v)); };

                reinterpret_cast<juce::PixelARGB*> (data.getPixelPointer (left + x, top + y))
                    ->setARGB (byte (alpha), byte (cover * colour.x), byte (cover * colour.y), byte (cover * colour.z));
            }
        });

        std::printf ("    %s %d/%d\n", run.photo, k + 1, run.frames);
        std::fflush (stdout);
    }

    return image;
}

bool save (const juce::Image& image, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream stream (file);

    if (file.hasFileExtension ("jpg"))
    {
        juce::JPEGImageFormat jpeg;
        jpeg.setQuality (0.93f);
        return stream.openedOk() && jpeg.writeImageToStream (image, stream);
    }

    return stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    if (argc < 2)
    {
        std::printf ("massif_photo <folder> [quality] [names]\n");
        return 1;
    }

    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    const auto quality = argc > 2 ? juce::String (argv[2]).getFloatValue() : 1.0f;
    const auto only = argc > 3 ? juce::StringArray::fromTokens (argv[3], ",", {}) : juce::StringArray();
    auto wanted = [&] (const char* name) { return only.isEmpty() || only.contains (name); };
    folder.createDirectory();

    const auto start = juce::Time::getMillisecondCounterHiRes();
    auto elapsed = [&] { return (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0; };

    if (wanted ("front"))
    {
        const auto ink = printLettering();
        const auto samples = std::max (1, (int) std::lround (64.0f * quality));

        for (const auto lit : { false, true })
        {
            const auto name = lit ? "front-lit.jpg" : "front-off.jpg";
            save (photographFront (lit, samples, ink), folder.getChildFile (name));
            std::printf ("%s (%.0f s)\n", name, elapsed());
            std::fflush (stdout);
        }
    }

    for (const auto* run : kRuns)
    {
        if (! wanted (run->photo))
            continue;

        const auto samples = std::max (1, (int) std::lround (196.0f * quality));
        save (photographRun (*run, samples), folder.getChildFile (juce::String (run->photo) + ".png"));
        std::printf ("%s.png (%.0f s)\n", run->photo, elapsed());
        std::fflush (stdout);
    }

    if (only.contains ("preview"))
    {
        auto front = juce::ImageCache::getFromFile (folder.getChildFile ("front-lit.jpg"));
        juce::Image preview (juce::Image::RGB, front.getWidth(), front.getHeight(), false, juce::SoftwareImageType());
        juce::Graphics g (preview);
        g.drawImageAt (front, 0, 0);

        for (const auto& control : controls())
        {
            if (control.run == nullptr)
                continue;

            const auto& run = *control.run;
            const auto strip = juce::ImageCache::getFromFile (folder.getChildFile (juce::String (run.photo) + ".png"));
            const auto pixels = (int) std::lround (run.frame.size() * (float) kPhotoScale);
            const auto degrees = degreesAt (control, 0.7f);
            const auto k = std::clamp ((int) std::lround ((degrees - run.fromDegrees) / (run.toDegrees - run.fromDegrees) * (float) (run.frames - 1)),
                                       0, run.frames - 1);
            const auto columns = std::min (run.frames, 8);

            if (strip.isValid())
                g.drawImage (strip, (int) std::lround ((control.x - run.frame.before) * (float) kPhotoScale),
                             (int) std::lround ((control.y - run.frame.before) * (float) kPhotoScale), pixels, pixels,
                             (k % columns) * pixels, (k / columns) * pixels, pixels, pixels);
        }

        save (preview, folder.getChildFile ("preview.png"));
    }

    return 0;
}
