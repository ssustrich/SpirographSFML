// spirograph_sfm3.cpp
// Build as C++17+ and link SFML 3 (graphics, window, system)

#include <SFML/Graphics.hpp>
#include <SFML/Graphics/Font.hpp>
#include <cmath>
#include <optional>
#include <string>
#include <cstdint>

static sf::Color hsv(float h, float s, float v, std::uint8_t a = 220) {
    // h in [0,360), s,v in [0,1]
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
    auto to8 = [](float u) { return static_cast<std::uint8_t>(std::round((u) * 255.f)); };
    return sf::Color(to8(r + m), to8(g + m), to8(b + m), a);
}

// Helper: make a Vector2f quickly
static inline sf::Vector2f V2(float x, float y) { return sf::Vector2f{x, y}; }

int main() {
    // Window setup (SFML 3 uses Vector for VideoMode size)
    constexpr unsigned kWidth = 1800, kHeight = 1000;
    sf::ContextSettings settings; settings.antiAliasingLevel = 8;
    sf::RenderWindow window(sf::VideoMode({ kWidth, kHeight }), "SFML 3 Spirograph", sf::State::Windowed, settings);
    //window.setFramerateLimit(120); // or 
    window.setVerticalSyncEnabled(true);

    // Spirograph parameters
    float R = 220.f;     // radius of fixed (large) circle
    float r = 70.f;      // radius of rolling (small) circle
    float d = 90.f;      // pen offset from small circle's center
    float speed = 4.2f;  // radians per second
   
    bool rainbow = true;
    float pixelsPerCycle = 500.f; // how many drawn pixels per full 360° hue
    float hueOffset = 0.f;        // shift the palette
    float pathLen = 0.f;          // accumulated path length

    float stroke = 6.f;     // thickness in pixels
    bool roundCaps = true;  // draw circles at the ends

    bool rollingOutside = false; // false=hypotrochoid (inside), true=epitrochoid (outside)
    bool tracing = true;

    const sf::Vector2f center = V2(kWidth * 0.5f, kHeight * 0.5f);

    // Big fixed circle
    sf::CircleShape bigCircle(R);
    bigCircle.setPointCount(180);
    bigCircle.setOrigin(V2(R, R));
    bigCircle.setPosition(center);
    bigCircle.setFillColor(sf::Color::Transparent);
    bigCircle.setOutlineThickness(2.f);
    bigCircle.setOutlineColor(sf::Color(180, 180, 180));

    // Small rolling circle
    sf::CircleShape smallCircle(r);
    smallCircle.setOrigin(V2(r, r));
    smallCircle.setFillColor(sf::Color::Transparent);
    smallCircle.setOutlineThickness(2.f);
    smallCircle.setOutlineColor(sf::Color(140, 200, 255));

    // Pen (drawing point)
    const float penRadius = 4.f;
    sf::CircleShape pen(penRadius);
    pen.setOrigin(V2(penRadius, penRadius));
    pen.setFillColor(sf::Color::Red);

    // RenderTexture to accumulate the trace (SFML 3: construct with Vector2u)
    sf::RenderTexture traceRT;
    traceRT.resize({ kWidth, kHeight }, settings);
    traceRT.setSmooth(true);
    traceRT.clear(sf::Color::Transparent);
    traceRT.display();
    sf::Sprite traceSprite(traceRT.getTexture());

    // HUD (Text requires a font at construction in SFML 3)
    sf::Font font;
    bool haveFont = font.openFromFile("c:/Users/maxxi/source/repos/SpirographSFML/x64/Debug/CONSOLA.TTF");// place a TTF beside your exe

    std::optional<sf::Text> hud;
    if (haveFont) {
        hud.emplace(font);                  // sf::Text text(font)
        hud->setCharacterSize(16);
        hud->setFillColor(sf::Color::White);
        hud->setPosition(V2(12.f, 10.f));
    }

    float t = 0.f;
    sf::Clock clock;

    // For drawing continuous lines on the render texture
    bool haveLast = false;
    sf::Vector2f lastPen{};

    // Main loop
    while (window.isOpen()) {
        // Events (SFML 3: std::optional + getIf/is)
        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                window.close();
            } else if (const auto* k = event->getIf<sf::Event::KeyPressed>()) {
                using KS = sf::Keyboard::Scancode;
                switch (k->scancode) {
                case KS::Up:     R += 5.f;  bigCircle.setRadius(R); bigCircle.setOrigin({ R,R }); break;
                case KS::Down:   R = std::max(5.f, R - 5.f); bigCircle.setRadius(R); bigCircle.setOrigin({ R,R }); break;
                case KS::Right:  r += 2.f;  smallCircle.setRadius(r); smallCircle.setOrigin({ r,r }); break;
                case KS::Left:   r = std::max(2.f, r - 2.f); smallCircle.setRadius(r); smallCircle.setOrigin({ r,r }); break;
                case KS::W:      d += 2.f;  break;
                case KS::S:      d = std::max(0.f, d - 2.f); break;
                case KS::Q:      speed += 0.1f; break;
                case KS::A:      speed = std::max(0.1f, speed - 0.1f); break;
                case KS::Escape: window.close();  break;
                case KS::Space:  tracing = !tracing; break;
                case KS::E:      rollingOutside = !rollingOutside; haveLast = false; break;
                case KS::C:      traceRT.clear(sf::Color::Transparent); traceRT.display(); haveLast = false; break;

                case KS::R:        rainbow = !rainbow; break;
                case KS::LBracket: pixelsPerCycle = std::max(50.f, pixelsPerCycle - 50.f); break;
                case KS::RBracket: pixelsPerCycle += 50.f; break;
                case KS::Hyphen:     hueOffset -= 10.f; break;
                case KS::Equal:     hueOffset += 10.f; break;// (= key)

                default: break;
                }
            }
        }

        // Time step
        float dt = clock.restart().asSeconds();
        t += speed * dt;

        // Rolling circle center and pen position
        float kR = rollingOutside ? (R + r) : (R - r);
        float theta = t;

        // Center of the small circle
        sf::Vector2f smallCenter = center + V2(kR * std::cos(theta), kR * std::sin(theta));
        smallCircle.setPosition(smallCenter);

        // Pen position (hypotrochoid/epitrochoid)
        float freq = kR / r; // (R ± r)/r
        float px, py;
        if (rollingOutside) {
            // Epitrochoid
            px = kR * std::cos(theta) - d * std::cos(freq * theta);
            py = kR * std::sin(theta) - d * std::sin(freq * theta);
        } else {
            // Hypotrochoid
            px = kR * std::cos(theta) + d * std::cos(freq * theta);
            py = kR * std::sin(theta) - d * std::sin(freq * theta);
        }
        sf::Vector2f penPos = center + V2(px, py);
        pen.setPosition(penPos);

        // Draw trace onto the render texture
    {
        // scope helps avoid accidentally drawing to window
            if (tracing) {
                // advance path length if you use rainbow-by-length
                float prevLen = pathLen;
                if (haveLast) pathLen += std::hypot(penPos.x - lastPen.x, penPos.y - lastPen.y);

                // colors (use your existing rainbow logic or a solid color)
                auto c0 = rainbow ? hsv(std::fmod((prevLen / pixelsPerCycle) * 360.f + hueOffset, 360.f), 1.f, 1.f)
                    : sf::Color(255, 100, 100, 220);
                auto c1 = rainbow ? hsv(std::fmod((pathLen / pixelsPerCycle) * 360.f + hueOffset, 360.f), 1.f, 1.f)
                    : sf::Color(255, 100, 100, 220);

                sf::Vector2f dir{ penPos.x - lastPen.x, penPos.y - lastPen.y };
                float len = std::hypot(dir.x, dir.y);

                if (haveLast && len > 0.0001f) {
                    // unit normal scaled to half the stroke
                    sf::Vector2f n{ -dir.y / len, dir.x / len };
                    n.x *= (stroke * 0.5f);
                    n.y *= (stroke * 0.5f);

                    // Quad corners (lastPen→penPos, left/right offset by ±n)
                    sf::Vertex quad[4] = {
                        sf::Vertex{ sf::Vector2f{ lastPen.x - n.x, lastPen.y - n.y }, c0 },
                        sf::Vertex{ sf::Vector2f{ lastPen.x + n.x, lastPen.y + n.y }, c0 },
                        sf::Vertex{ sf::Vector2f{  penPos.x + n.x,  penPos.y + n.y }, c1 },
                        sf::Vertex{ sf::Vector2f{  penPos.x - n.x,  penPos.y - n.y }, c1 }
                    };

                    // Draw as a convex quad (two triangles)
                    traceRT.draw(quad, 4, sf::PrimitiveType::TriangleFan);

                    if (roundCaps) {
                        float rcap = stroke * 0.5f;
                        sf::CircleShape cap(rcap);
                        cap.setOrigin({ rcap, rcap });

                        cap.setFillColor(c0); cap.setPosition(lastPen); traceRT.draw(cap);
                        cap.setFillColor(c1); cap.setPosition(penPos);  traceRT.draw(cap);
                    }
                }

                haveLast = true;
                lastPen = penPos;
                traceRT.display();
            }
            else {
                haveLast = false;
            }
    }

        // Update HUD
        if (hud) {
            std::string mode = rollingOutside ? "Outside (Epitrochoid)" : "Inside (Hypotrochoid)";
            hud->setString(
                "Space: toggle tracing  |  E: toggle inside/outside  |  C: clear  |  Esc: quit\n" +
                std::string("R=") + std::to_string((int)R) + "  r=" + std::to_string((int)r) +
                "  d=" + std::to_string((int)d) + "  speed=" + std::to_string(speed) +
                "  mode=" + mode
            );
        }

        // Render
        window.clear(sf::Color(15, 18, 22));
        window.draw(traceSprite);          // accumulated path
        window.draw(bigCircle);
        window.draw(smallCircle);

        // Arm from small circle center to pen
        std::array<sf::Vertex, 2> arm{
            sf::Vertex{ smallCircle.getPosition(), sf::Color{255, 100, 100, 220} },
            sf::Vertex{ penPos,  sf::Color{255, 100, 100, 220} }
        };
        window.draw(arm.data(), 2, sf::PrimitiveType::Lines);

        window.draw(pen);
        if (hud) window.draw(*hud);
        window.display();
    }

    return 0;
}
