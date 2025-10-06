// main.cpp — Nested Spirograph (SFML 3.0) — fixed & tweaked
// Build: C++17+ and link SFML 3 (graphics, window, system)

#include <SFML/Graphics.hpp>
#include <cmath>
#include <optional>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <algorithm>

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

static void drawThickSegment(sf::RenderTarget& target,
    const sf::Vector2f& a, const sf::Vector2f& b,
    float stroke, const sf::Color& ca, const sf::Color& cb)
{
    sf::Vector2f d{ b.x - a.x, b.y - a.y };
    float len = std::hypot(d.x, d.y);
    if (len < 0.0001f) return;

    sf::Vector2f n = { -d.y / len, d.x / len };
    n.x *= (stroke * 0.5f);
    n.y *= (stroke * 0.5f);

    sf::Vertex quad[4] = {
        sf::Vertex{ sf::Vector2f{ a.x - n.x, a.y - n.y }, ca },
        sf::Vertex{ sf::Vector2f{ a.x + n.x, a.y + n.y }, ca },
        sf::Vertex{ sf::Vector2f{ b.x + n.x, b.y + n.y }, cb },
        sf::Vertex{ sf::Vector2f{ b.x - n.x, b.y - n.y }, cb }
    };
    target.draw(quad, 4, sf::PrimitiveType::TriangleFan);

    float rcap = stroke * 0.5f;
    sf::CircleShape cap(rcap);
    cap.setOrigin({ rcap, rcap });
    cap.setFillColor(ca); cap.setPosition(a); target.draw(cap);
    cap.setFillColor(cb); cap.setPosition(b); target.draw(cap);
}

// ---------- model ----------
struct Stage {
    int   level = 1;     // 1 = first nested disc
    float r;             // rolling disc radius
    float d;             // pen offset (used only by the LAST stage)
    bool  outside;       // false = inside roll; true = outside roll
    float speed;         // radians/sec (negative = reverse)
    float phase;         // start angle (radians)

    // (style for last stage trace — kept for future use)
    float stroke = 6.f;
    bool  rainbow = true;
    float pixelsPerCycle = 600.f;
    float hueOffset = 0.f;

    // visuals
    sf::CircleShape disc;

    Stage(int lvl, float rr, float dd, bool out, float spd, float ph = -3.14159f / 2.f)
        : level(lvl), r(rr), d(dd), outside(out), speed(spd), phase(ph), disc(rr)
    {
        disc.setOrigin({ r, r });
        disc.setFillColor(sf::Color::Transparent);
        disc.setOutlineThickness(2.f);
        disc.setOutlineColor(sf::Color(140, 200, 255));
        disc.setPointCount(140);
    }
};

// Nested centers + pen with per-stage speeds.
// Returns local coords (add screen center to draw).
static sf::Vector2f nestedPenAndCenters_perStageSpeed(float R,
    const std::vector<Stage>& stages,
    float t,
    std::vector<sf::Vector2f>* outCenters)
{
    if (outCenters) outCenters->clear();
    sf::Vector2f acc{ 0.f, 0.f };
    float baseRadius = R;

    for (std::size_t j = 0; j < stages.size(); ++j) {
        const Stage& s = stages[j];
        float alpha = s.speed * t + s.phase;
        float kappa = s.outside ? (baseRadius + s.r) : (baseRadius - s.r);

        acc.x += kappa * std::cos(alpha);
        acc.y += kappa * std::sin(alpha);
        if (outCenters) outCenters->push_back(acc);

        bool last = (j + 1 == stages.size());
        if (last) {
            float freq = kappa / s.r;
            float beta = freq * alpha;
            float ox, oy;
            if (s.outside) { ox = -s.d * std::cos(beta); oy = -s.d * std::sin(beta); }
            else { ox = s.d * std::cos(beta); oy = -s.d * std::sin(beta); }
            acc.x += ox; acc.y += oy;
        }
        else {
            baseRadius = s.r; // next stage rolls on this disc
        }
    }
    return acc;
}

// Returns the pen *local* position at time t (no centers allocated)
static inline sf::Vector2f penAtTime(float R,
    const std::vector<Stage>& chain,
    float t) {
    return nestedPenAndCenters_perStageSpeed(R, chain, t, nullptr);
}

// ---------- help overlay ----------
struct HelpOverlay {
    bool visible = false;
    std::optional<sf::Text> text;   // sf::Text has no default ctor in SFML 3
    sf::RectangleShape bg;

    static constexpr float kPad = 16.f;

    void init(const sf::Font& font, const sf::Vector2f& pos = { 40.f, 40.f }) {
        text.emplace(font);                 // construct with a font
        text->setCharacterSize(18);
        text->setFillColor(sf::Color::White);
        text->setPosition(pos);
        rebuild();
    }

    void rebuild() {
        if (!text) return;

        text->setString(
            "Controls\n"
            "------------\n"
            "General\n"
            "  Esc          Quit\n"
            "  Space        Trace on/off\n"
            "  C            Clear trace\n"
            "  P            Save PNG\n"
            "  M            Show/hide mechanism\n"
            "  H / F1       Toggle this help\n"
            "\nPer-stage editing\n"
            "  PgUp / PgDn  Selected Stage +/-\n"
            "  [ / ]        Speed - / +\n"
            "  Z            Flip direction\n"
            "\nBase circle\n"
            "  Up / Down    R +/-\n"
        );

        // Use GLOBAL bounds (SFML 3: Rect has .position and .size)
        const auto gb = text->getGlobalBounds();
        bg.setSize({ gb.size.x + kPad * 2.f, gb.size.y + kPad * 2.f });
        bg.setPosition({ gb.position.x - kPad, gb.position.y - kPad });
        bg.setFillColor(sf::Color(0, 0, 0, 200));
        bg.setOutlineThickness(2.f);
        bg.setOutlineColor(sf::Color(255, 255, 255, 80));
    }

    void setPosition(const sf::Vector2f& pos) {
        if (!text) return;
        text->setPosition(pos);
        // keep box in sync
        const auto gb = text->getGlobalBounds();
        bg.setSize({ gb.size.x + kPad * 2.f, gb.size.y + kPad * 2.f });
        bg.setPosition({ gb.position.x - kPad, gb.position.y - kPad });
    }

    void draw(sf::RenderTarget& rt) const {
        if (!visible || !text) return;
        rt.draw(bg);
        rt.draw(*text);
    }
};

int main() {
    constexpr unsigned kW = 1280, kH = 900;

    // --- trace resolution control ---
    float maxPixelStep = 1.0f; // max pixels per sub-segment; lower = smoother
    int   maxSubsteps = 256;  // safety cap
    float lastT = 0.f;         // keep last time for sub-stepping

    // Anti-aliased window (MSAA)
    sf::ContextSettings settings; settings.antiAliasingLevel = 8;
    sf::RenderWindow window(sf::VideoMode({ kW, kH }), "Nested Spirograph — per-stage speed (SFML 3)",
        sf::State::Windowed, settings);
    window.setFramerateLimit(120);

    const sf::Vector2f screenCenter = V2(kW * 0.5f, kH * 0.5f);

    // Base circle
    float R = 200.f;
    sf::CircleShape big(R);
    big.setOrigin({ R, R });
    big.setPosition(screenCenter);
    big.setFillColor(sf::Color::Transparent);
    big.setOutlineThickness(2.f);
    big.setOutlineColor(sf::Color(180, 180, 180, 10));
    big.setPointCount(220);

    // Stages (distinct speeds; negative reverses) — cleaner speeds + level=1..N
    std::vector<Stage> chain;
    {
        float radius = R;
        const float radiusDiv = 3.f;
        const float baseSpeed = -4.00f; // rad/s
        const float decay = 0.75f; // each level slower
        const int   count = 10;

        for (int i = 0; i < count; ++i) {
            radius /= radiusDiv;
            float s = pow(baseSpeed, i);
            //float s = baseSpeed * std::pow(decay, static_cast<float>(i)) * ((i % 2) ? -1.f : 1.f);
            chain.emplace_back(/*level*/ i + 1,
                /*r*/ radius,
                /*d*/ 0.f,               // set last stage's d below
                /*outside*/ true,
                /*speed*/ s);
        }
        if (!chain.empty()) {
            // Give the last stage a real pen offset so we trace something non-trivial
            chain.back().d = chain.back().r * 0.75f;
        }
    }

    // Trace surface
    sf::RenderTexture traceRT;
    traceRT.resize({ kW, kH }, settings);
    traceRT.setSmooth(true);
    traceRT.clear(sf::Color::Transparent);
    traceRT.display();
    sf::Sprite traceSprite(traceRT.getTexture());

    // HUD
    sf::Font font;
    bool haveFont =
        font.openFromFile("assets/fonts/CONSOLA.TTF") ||
        font.openFromFile("Consola.ttf") ||
        font.openFromFile("consola.ttf") ||
        font.openFromFile("arial.ttf");

    std::optional<sf::Text> hud;
    if (haveFont) {
        hud.emplace(font);
        hud->setCharacterSize(16);
        hud->setFillColor(sf::Color::White);
        hud->setPosition({ 12.f, 10.f });
    }

    // Help
    HelpOverlay help;
    if (haveFont) help.init(font);

    // State
    bool tracing = true;
    bool showMechanism = true;
    int  sel = 0;    // selected stage
    float t = 0.f;
    sf::Clock clock;

    // Rainbow path control
    float pathLen = 0.f;
    float pixelsPerCycle = 600.f;
    float hueOffset = 0.f;
    float stroke = 2.f;  // use this global stroke for the thick segments

    bool haveLast = false;
    sf::Vector2f lastPen{};

    auto wrapIndex = [&](int i) {
        int n = static_cast<int>(chain.size());
        if (n == 0) return 0;
        i %= n; if (i < 0) i += n;
        return i;
        };

    auto updateHud = [&] {
        if (!hud) return;
        std::ostringstream ss;
        ss << "Selection: " << chain[sel].level << "\n"
            << "Speed: " << std::fixed << std::setprecision(2) << chain[sel].speed << "\n"
            << "Size: " << std::fixed << std::setprecision(2) << chain[sel].r << "\n"
            << "Outside Roll: " << (chain[sel].outside ? "true" : "false") << "\n"
            << "H / F1 help\n";
        hud->setString(ss.str());
        };
    updateHud();

    while (window.isOpen()) {
        // ----- events -----
        while (const auto ev = window.pollEvent()) {
            if (ev->is<sf::Event::Closed>()) { window.close(); continue; }

            if (const auto* k = ev->getIf<sf::Event::KeyPressed>()) {
                using KS = sf::Keyboard::Scancode;

                switch (k->scancode) {
                    // app control
                case KS::Escape:   window.close(); break;
                case KS::Space:    tracing = !tracing; break;
                case KS::M:        showMechanism = !showMechanism; break;
                case KS::C:
                    traceRT.clear(sf::Color::Transparent); traceRT.display();
                    haveLast = false; pathLen = 0.f;
                    break;

                    // selection via PageUp/PageDown
                case KS::PageUp:
                    sel = wrapIndex(sel + 1); updateHud(); break;
                case KS::PageDown:
                    sel = wrapIndex(sel - 1); updateHud(); break;

                    // toggle inside/outside on selected stage
                case KS::E:
                    chain[sel].outside = !chain[sel].outside; updateHud(); break;

                    // save PNG
                case KS::P: {
                    auto img = traceRT.getTexture().copyToImage();
                    static int n = 0; std::ostringstream name;
                    name << "nested_pss_" << std::setw(3) << std::setfill('0') << n++ << ".png";
                    img.saveToFile(name.str());
                    break;
                }

                          // help
                case KS::H:
                case KS::F1:
                    help.visible = !help.visible;
                    break;

                    // base radius
                case KS::Up:
                    R += 5.f; big.setRadius(R); big.setOrigin({ R,R }); updateHud(); break;
                case KS::Down:
                    R = std::max(20.f, R - 5.f); big.setRadius(R); big.setOrigin({ R,R }); updateHud(); break;

                    // per-stage speed (correct bracket names in SFML 3)
                case KS::LBracket:  chain[sel].speed -= 0.1f; updateHud(); break; // [
                case KS::RBracket:  chain[sel].speed += 0.1f; updateHud(); break; // ]
                case KS::Z:         chain[sel].speed = -chain[sel].speed; updateHud(); break;

                default: break;
                }
            }
        }

        // ----- update -----
        float dt = clock.restart().asSeconds();
        if (!help.visible) { // pause sim while help is visible (optional)
            t += dt;
        }

        // centers & pen
        std::vector<sf::Vector2f> centers;
        sf::Vector2f penLocal = nestedPenAndCenters_perStageSpeed(R, chain, t, &centers);
        sf::Vector2f penPos = screenCenter + penLocal;

        // ======== trace (adaptive sub-sampling) ========
        if (tracing && !help.visible) {
            // where we *want* to be this frame
            const sf::Vector2f currPen = screenCenter + penAtTime(R, chain, t);

            if (!haveLast) {
                // first point in a run
                haveLast = true;
                lastPen = currPen;
                lastT = t;
            }

            // Decide how many sub-steps based on screen distance
            const float dist = std::hypot(currPen.x - lastPen.x, currPen.y - lastPen.y);
            int steps = static_cast<int>(std::ceil(dist / std::max(0.1f, maxPixelStep)));
            steps = std::clamp(steps, 1, maxSubsteps);

            sf::Vector2f prev = lastPen;

            for (int i = 1; i <= steps; ++i) {
                float s = static_cast<float>(i) / static_cast<float>(steps);
                float ti = lastT + (t - lastT) * s;

                sf::Vector2f p = screenCenter + penAtTime(R, chain, ti);

                // rainbow by length (small segments, smooth gradient)
                float prevLen = pathLen;
                pathLen += std::hypot(p.x - prev.x, p.y - prev.y);

                float h0 = std::fmod((prevLen / pixelsPerCycle) * 360.f + hueOffset, 360.f);
                float h1 = std::fmod((pathLen / pixelsPerCycle) * 360.f + hueOffset, 360.f);
                sf::Color c0 = hsv(h0, 1.f, 1.f);
                sf::Color c1 = hsv(h1, 1.f, 1.f);

                // USE THE GLOBAL stroke (tweak #2)
                drawThickSegment(traceRT, prev, p, stroke, c0, c1);

                prev = p;
            }

            // finalize for next frame
            lastPen = currPen;
            lastT = t;
            traceRT.display();
        }
        else {
            haveLast = false; // stop the run
        }

        // Update small disc positions once per frame (draw later)
        for (std::size_t i = 0; i < chain.size(); ++i) {
            chain[i].disc.setPosition(screenCenter + centers[i]);
        }

        // ----- draw -----
        window.clear(sf::Color(15, 18, 22));
        window.draw(traceSprite);
        window.draw(big);

        if (showMechanism) {
            for (std::size_t i = 0; i < chain.size(); ++i) {
                // highlight selected
                chain[i].disc.setOutlineColor(i == static_cast<std::size_t>(sel)
                    ? sf::Color(255, 230, 120)
                    : sf::Color(140, 200, 255));
                window.draw(chain[i].disc);

                sf::Vector2f from = screenCenter + centers[i];
                sf::Vector2f to = (i + 1 < centers.size())
                    ? (screenCenter + centers[i + 1])
                    : penPos;
                sf::Vertex arm[2] = {
                    sf::Vertex{ from, sf::Color(120,200,140) },
                    sf::Vertex{ to,   sf::Color(120,200,140) }
                };
                window.draw(arm, 2, sf::PrimitiveType::Lines);
            }
        }

        // pen dot
        sf::CircleShape penDot(4.f);
        penDot.setOrigin({ 4.f, 4.f });
        penDot.setFillColor(sf::Color::Red);
        penDot.setPosition(penPos);
        window.draw(penDot);

        if (hud)  window.draw(*hud);
        help.draw(window);            // draw help overlay LAST
        window.display();
    }

    return 0;
}
