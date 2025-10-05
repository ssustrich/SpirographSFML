// nested_spiro_sfml3.cpp
// Build with: C++17+ and SFML 3 (graphics, window, system)

#include <SFML/Graphics.hpp>
#include <cmath>
#include <optional>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdint>

// ---------- helpers ----------
static inline sf::Vector2f V2(float x, float y) { return { x, y }; }

static sf::Color hsv(float h, float s, float v, std::uint8_t a = 230) {
    float c = v * s;
    float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
    float m = v - c;
    float r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else { r = c; g = 0; b = x; }
    auto to8 = [](float u) { return static_cast<std::uint8_t>(std::round(u * 255.f)); };
    return sf::Color(to8(r + m), to8(g + m), to8(b + m), a);
}

// Draw one thick gradient segment into a RenderTarget
static void drawThickSegment(sf::RenderTarget& target,
    const sf::Vector2f& a, const sf::Vector2f& b,
    float stroke, const sf::Color& ca, const sf::Color& cb) {
    sf::Vector2f d = b - a;
    float len = std::hypot(d.x, d.y);
    if (len < 0.0001f) return;

    sf::Vector2f n = { -d.y / len, d.x / len };
    n.x *= (stroke * 0.5f);
    n.y *= (stroke * 0.5f);

    sf::Vertex quad[4] = {
        sf::Vertex{ a - n, ca }, sf::Vertex{ a + n, ca },
        sf::Vertex{ b + n, cb }, sf::Vertex{ b - n, cb }
    };
    target.draw(quad, 4, sf::PrimitiveType::TriangleFan);

    // round caps hide tiny gaps at joints
    float rcap = stroke * 0.5f;
    sf::CircleShape cap(rcap);
    cap.setOrigin({ rcap, rcap });
    cap.setFillColor(ca); cap.setPosition(a); target.draw(cap);
    cap.setFillColor(cb); cap.setPosition(b); target.draw(cap);
}

// ---------- nested model ----------
struct Stage {
    float r;       // rolling circle radius (this stage)
    float d;       // pen offset if this is the LAST stage; ignored otherwise
    bool outside;  // false=rolls inside previous circle, true=outside

    // render styling for this stage's trace (only used by last stage)
    float stroke = 3.f;
    bool  rainbow = true;
    float pixelsPerCycle = 500.f;
    float hueOffset = 0.f;

    // visuals
    sf::CircleShape disc; // the rolling circle itself
    Stage(float rr, float dd, bool out) : r(rr), d(dd), outside(out), disc(rr) {
        disc.setOrigin({ r, r });
        disc.setFillColor(sf::Color::Transparent);
        disc.setOutlineThickness(2.f);
        disc.setOutlineColor(sf::Color(140, 200, 255));
        disc.setPointCount(140);
    }
};

// Compute the nested pen position and stage centers.
// Conventions (screen coords): +x right, +y down. Angles are standard radians.
static sf::Vector2f nestedPenAndCenters(float R,
    const std::vector<Stage>& stages,
    float theta0,
    std::vector<sf::Vector2f>* outCenters) {
    if (outCenters) outCenters->clear();
    sf::Vector2f acc = { 0.f, 0.f }; // accumulated position from origin
    float alpha = theta0;          // driving angle for this stage (chained)

    float baseRadius = R;
    for (std::size_t j = 0; j < stages.size(); ++j) {
        const Stage& s = stages[j];
        float kappa = s.outside ? (baseRadius + s.r) : (baseRadius - s.r); // center path radius
        // center of this rolling disc relative to previous center, at angle 'alpha'
        acc += V2(kappa * std::cos(alpha), kappa * std::sin(alpha));
        if (outCenters) outCenters->push_back(acc);

        // internal spin ratio for what's attached to this disc (next stage or pen)
        float freq = kappa / s.r;
        float beta = freq * alpha; // chained rolling without slip

        bool last = (j + 1 == stages.size());
        if (last) {
            // Apply the pen offset on the LAST stage
            // For outside roll: subtract (cos,sin); for inside: add cos, subtract sin
            float ox, oy;
            if (s.outside) {
                ox = -s.d * std::cos(beta);
                oy = -s.d * std::sin(beta);
            }
            else {
                ox = s.d * std::cos(beta);
                oy = -s.d * std::sin(beta);
            }
            acc += V2(ox, oy);
        }
        else {
            // Next stage rolls on THIS disc: its center angle is this disc's spin angle
            alpha = beta;
            baseRadius = s.r; // next stage rolls on current disc with radius r_j
        }
    }
    return acc;
}

int main() {
    constexpr unsigned kW = 1280, kH = 900;
    sf::ContextSettings settings; settings.antiAliasingLevel = 8;
    sf::RenderWindow window(sf::VideoMode({ kW, kH }), "Nested Spirograph (SFML 3)",
        sf::State::Windowed, settings);
    window.setFramerateLimit(120);

    const sf::Vector2f center = V2(kW * 0.5f, kH * 0.5f);

    // Big fixed circle (base)
    float R = 260.f;
    sf::CircleShape big(R);
    big.setOrigin({ R, R });
    big.setPosition(center);
    big.setFillColor(sf::Color::Transparent);
    big.setOutlineThickness(2.f);
    big.setOutlineColor(sf::Color(180, 180, 180));
    big.setPointCount(220);

    // Define a nested chain: stage1 rolls on big, stage2 on stage1, stage3 on stage2.
    std::vector<Stage> chain;
    chain.emplace_back(/*r*/ 90.f, /*d*/ 60.f, /*outside*/ false);  // inside big
    chain.emplace_back(/*r*/ 50.f, /*d*/ 35.f, /*outside*/ true);   // outside the previous disc
    chain.emplace_back(/*r*/ 30.f, /*d*/ 55.f, /*outside*/ false);  // inside the previous disc

    // Trace surface (smooth + MSAA)
    sf::RenderTexture traceRT;
    traceRT.resize({ kW, kH }, settings);
    traceRT.setSmooth(true);
    traceRT.clear(sf::Color::Transparent);
    traceRT.display();
    sf::Sprite traceSprite(traceRT.getTexture());

    // HUD
    sf::Font font;
    bool haveFont = font.openFromFile("Consola.ttf")
        || font.openFromFile("consola.ttf")
        || font.openFromFile("arial.ttf");
    std::optional<sf::Text> hud;
    if (haveFont) {
        hud.emplace(font);
        hud->setCharacterSize(16);
        hud->setFillColor(sf::Color::White);
        hud->setPosition({ 12.f, 10.f });
    }

    // Controls/state
    bool tracing = true;
    bool showMechanism = true;
    float speed = 0.9f;          // base angular speed (rad/s) for first stage
    float t = 0.f;
    sf::Clock clock;

    // Rainbow-by-length
    float pathLen = 0.f;
    float pixelsPerCycle = 600.f;
    float hueOffset = 0.f;
    float stroke = 6.f;

    bool haveLast = false;
    sf::Vector2f lastPen{};

    auto updateHud = [&] {
        if (!hud) return;
        std::ostringstream ss;
        ss << "Space: trace on/off | M: toggle mechanism | C: clear | P: save PNG\n"
            << "E: flip in/out on last stage | Arrows: R +/- | +/-: speed +/-\n"
            << "R=" << int(R) << "  speed=" << speed
            << "  stages=" << chain.size();
        hud->setString(ss.str());
        };
    updateHud();

    while (window.isOpen()) {
        while (const auto ev = window.pollEvent()) {
            if (ev->is<sf::Event::Closed>()) window.close();
            else if (const auto* k = ev->getIf<sf::Event::KeyPressed>()) {
                using KS = sf::Keyboard::Scancode;
                if (k->scancode == KS::Escape) window.close();
                if (k->scancode == KS::Space) { tracing = !tracing; }
                if (k->scancode == KS::M) { showMechanism = !showMechanism; }
                if (k->scancode == KS::C) {
                    traceRT.clear(sf::Color::Transparent); traceRT.display();
                    haveLast = false; pathLen = 0.f;
                }
                if (k->scancode == KS::P) {
                    auto img = traceRT.getTexture().copyToImage();
                    static int n = 0; std::ostringstream name;
                    name << "nested_spiro_" << std::setw(3) << std::setfill('0') << n++ << ".png";
                    img.saveToFile(name.str());
                }
                if (k->scancode == KS::E) {
                    // flip inside/outside on the last stage for fun
                    chain.back().outside = !chain.back().outside; haveLast = false;
                }
                if (k->scancode == KS::Up) { R += 5.f;  big.setRadius(R); big.setOrigin({ R,R }); updateHud(); }
                if (k->scancode == KS::Down) { R = std::max(20.f, R - 5.f); big.setRadius(R); big.setOrigin({ R,R }); updateHud(); }
                if (k->scancode == KS::Equal) { speed += 0.1f; updateHud(); }     // '=' key
                if (k->scancode == KS::Hyphen) { speed = std::max(0.1f, speed - 0.1f); updateHud(); }
            }
        }

        float dt = clock.restart().asSeconds();
        t += speed * dt;

        // Compute centers & pen
        std::vector<sf::Vector2f> centers;
        sf::Vector2f penLocal = nestedPenAndCenters(R, chain, t, &centers);
        sf::Vector2f penPos = center + penLocal;

        // Rainbow/thick trace
        if (tracing) {
            float prevLen = pathLen;
            if (haveLast) pathLen += std::hypot(penPos.x - lastPen.x, penPos.y - lastPen.y);

            float h0 = std::fmod((prevLen / pixelsPerCycle) * 360.f + hueOffset, 360.f);
            float h1 = std::fmod((pathLen / pixelsPerCycle) * 360.f + hueOffset, 360.f);
            sf::Color c0 = hsv(h0, 1.f, 1.f);
            sf::Color c1 = hsv(h1, 1.f, 1.f);

            if (haveLast) {
                drawThickSegment(traceRT, lastPen, penPos, chain.back().stroke, c0, c1);
            }
            haveLast = true;
            lastPen = penPos;
            traceRT.display();
        }
        else {
            haveLast = false;
        }

        // Update rolling discs' on-screen positions
        for (std::size_t i = 0; i < chain.size(); ++i) {
            chain[i].disc.setPosition(center + centers[i]);
        }

        // Draw
        window.clear(sf::Color(15, 18, 22));
        window.draw(traceSprite);
        window.draw(big);

        if (showMechanism) {
            // small circles and arm lines
            for (std::size_t i = 0; i < chain.size(); ++i) {
                window.draw(chain[i].disc);
                // arm line from disc center to next element (either next disc center or pen)
                sf::Vector2f from = center + centers[i];
                sf::Vector2f to = (i + 1 < centers.size())
                    ? (center + centers[i + 1])
                    : penPos;
                sf::Vertex arm[2] = { sf::Vertex{ from, sf::Color(120,200,140) },
                                      sf::Vertex{ to,   sf::Color(120,200,140) } };
                window.draw(arm, 2, sf::PrimitiveType::Lines);
            }
        }

        // draw the red pen dot
        sf::CircleShape penDot(4.f);
        penDot.setOrigin({ 4.f, 4.f });
        penDot.setFillColor(sf::Color::Red);
        penDot.setPosition(penPos);
        window.draw(penDot);

        if (hud) window.draw(*hud);
        window.display();
    }
    return 0;
}
