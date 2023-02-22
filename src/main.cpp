#include <SFML/Graphics.hpp>
#include <iostream>
#include <utility>
#include <vector>
#include <deque>
#include <cmath>
#include <map>
#include <set>
#include <complex>
#include <filesystem>
#define CNum complex<double>

/* CMAKE COMMANDS:
 * cmake -S . -B build
 * cmake --build build --config Release */

const int IMG_HEIGHT = 720;
const int IMG_WIDTH = 960;
const int IMG_SIZE = IMG_WIDTH * IMG_HEIGHT;
const int MIN_SAMPLE_POINTS = 4;
const int SAMPLE_SPACING = 20;
const int EVAL_SPACING = 1;
const int BG_OPACITY = 0; // 0 = invisible, 255 = solid
using namespace std;

struct Frame {
    sf::Image image;
    sf::Texture texture;

    explicit Frame(const string& path) {
        image.loadFromFile(path);
        texture.loadFromImage(image);
    }
};

// barycentric lagrangian interpolation
struct BLI {
    BLI() = delete;

    // construct chebyshev 2nd kind distribution for 0,...,n points in [-1, 1]
    static vector<double> chebyshev2(int n) {
        vector<double> xVec;
        xVec.reserve(n+1);
        for (int k = 0; k <= n; ++k)
            xVec.push_back(cos(k * M_PI / n));
        return xVec;
    }

    // find all w_i for f(x_i) = y_i where x_i is real on chebyshev 2nd kind distribution and y_i is complex
    // assumes points are distributed in chebyshev distribution
    static CNum eval(const vector<CNum>& yVec, double x) {
        int n = (int)yVec.size() - 1;
        auto xVec = chebyshev2(n);
        CNum num = {0, 0};
        CNum den = {0, 0};
        for (int i = 0; i <= n; ++i) {
            CNum w = i == 0 || i == n? 0.5 : 1;
            w = i & 1? -w : w;
            double div = 1.0 / (x - xVec[i]);
            num = num + (w * yVec[i] * div);
            den = den + (w * div);
        }
        return num / den;
    }
};

struct Polygon {
    vector<pair<int,int>> points;
    Polygon() = default;

    // create polygon from edge points
    explicit Polygon(vector<pair<int,int>> edgePoints) : points(move(edgePoints)) {
        points.push_back(points[0]);
    }

    // get point p proportional along polygon, p in [0, 1]
    [[nodiscard]] pair<double,double> getPoint(double p) const {
        if (p >= 1 || p <= 0)
            return make_pair((double)points.front().first, (double)points.front().second);
        p *= (points.size() - 1);
        int i = floor(p);
        return make_pair(
            (double)points[i].first + ((double)(points[i + 1].first - points[i].first) * (p - i)),
            (double)points[i].second + ((double)(points[i + 1].second - points[i].second) * (p - i))
        );
    }
};

// creates a vector of polygon outlining the perimeter of islands of black
vector<Polygon> createIslandPolygons(sf::Image& img) {
    vector<Polygon> islandPolygons;
    char visited[IMG_HEIGHT][IMG_WIDTH]; // 2 bits: visited, white
    printf("Getting Image Data\n");
    for (int y = 0; y < IMG_HEIGHT; ++y)
        for (int x = 0; x < IMG_WIDTH; ++x)
            visited[y][x] = (char)(img.getPixel(x, y).r > 127);
    printf("Traversing Islands\n");
    auto isEdgePoint = [&](int x, int y) {
        return (visited[y][x] & 1) == 0 && (
                x == 0 || y == 0 || x == IMG_WIDTH - 1 || y == IMG_HEIGHT - 1
                || visited[y-1][x] & 1 || visited[y][x-1] & 1 || visited[y+1][x] & 1 || visited[y][x+1] & 1
            );
    };
    for (int y = 0; y < IMG_HEIGHT; ++y) {
        for (int x = 0; x < IMG_WIDTH; ++x) {
            if (visited[y][x] || !isEdgePoint(x, y))
                continue;

            // get edge clockwise
            vector<pair<int,int>> path;
            path.emplace_back(x, y);
            const pair<int,int> offsets[] = {{0,1}, {1,1}, {1,0}, {1,-1}, {0,-1}, {-1,-1}, {-1,0}, {-1,1}};
            int dir = 2;
            int px = x;
            int py = y;
            set<long long> edges;
            while (true) {
                // try all 8 directions (7 if origin point to prevent reversal)
                bool searching = true;
                int dirCount = 8 - (px == x && py == y);
                for (int i = 0; i < dirCount && searching; ++i) {
                    dir = (dir + 1) & 0b111;
                    int qx = px + offsets[dir].first;
                    int qy = py + offsets[dir].second;
                    if (qx < 0 || qy < 0 || qx >= IMG_WIDTH || qy >= IMG_HEIGHT)
                        continue;
                    if (visited[qy][qx] & 0b11)
                        continue;
                    if (!isEdgePoint(qx, qy))
                        continue;
                    long long e = (long long)IMG_SIZE * (py * IMG_WIDTH + px) + qy * IMG_WIDTH + qx;
                    if (edges.count(e))
                        continue;
                    edges.insert(e);
                    path.emplace_back(qx, qy);
                    px = qx;
                    py = qy;
                    searching = false;
                }
                if (searching)
                    break;
                dir = (dir + 4) & 0b111;
            }
            if (path.back().first != path.front().first && path.back().second != path.front().second) {
                path.push_back(path.front());
            }
            for (auto& p : path) {
                visited[p.second][p.first] |= 0b10;
            }
            islandPolygons.emplace_back(move(path));
        }
    }
    return islandPolygons;
}

struct drawPolygonArgs {

};

sf::Mutex mutex;

void drawPolygon(sf::RenderTexture& renderTexture, const Polygon& poly, const sf::Color color) {
    // get sample points from polygon
    int samplePoints = (int)poly.points.size() / SAMPLE_SPACING;
    vector<double> xVec;
    xVec.reserve(samplePoints);
    for (int i = 0; i < samplePoints; ++i)
        xVec.push_back(((double) i / (samplePoints - 1)) * 2 - 1);
    vector<CNum> yVec;
    yVec.reserve(xVec.size());
    for (double& x : xVec) {
        pair<double, double> point = poly.getPoint(x * 0.5 + 0.5);
        yVec.emplace_back(point.first, point.second);
    }

    // evaluate barycentric lagrangian interpolated polynomial
    int evalPoints = max(3, (int)poly.points.size() / EVAL_SPACING);
    vector<double> evalVec = BLI::chebyshev2(evalPoints-1);
    vector<CNum> evalRes;
    evalRes.reserve(evalVec.size());
    for (double x : evalVec)
        evalRes.push_back(BLI::eval(yVec, x));

    // render points
    sf::CircleShape circle;
    circle.setRadius(2.f);
    circle.setOrigin(circle.getRadius(), circle.getRadius());
    circle.setFillColor(color);
    for (const CNum& point : evalRes) {
        circle.setPosition((float) point.real(), (float) point.imag());
        renderTexture.draw(circle);
    }
    circle.setRadius(4.f);
    circle.setOrigin(circle.getRadius(), circle.getRadius());
    for (const CNum& point : yVec) {
        circle.setPosition((float)point.real(), (float)point.imag());
        renderTexture.draw(circle);
    }
}

string createFrameId(int id) {
    string idStr;
    for (int j = 0; j < 5; ++j) {
        idStr.push_back((char)('0' + id % 10));
        id /= 10;
    }
    reverse(idStr.begin(), idStr.end());
    return idStr;
}

void playVideo()
{
    sf::RenderWindow window = sf::RenderWindow{ {  IMG_WIDTH, IMG_HEIGHT }, "CMake SFML Project" };
    window.setFramerateLimit(30);
    sf::RenderTexture videoTexture;
    videoTexture.create(IMG_WIDTH, IMG_HEIGHT);
    sf::RenderTexture polyTexture;
    polyTexture.create(IMG_WIDTH, IMG_HEIGHT);
    sf::RenderTexture outputTexture;
    outputTexture.create(IMG_WIDTH, IMG_HEIGHT);
    const int FRAMES = 6572;
    const int BUFFER_SIZE = 20;
    deque<Frame> frameBuffer;
    int bufferedFrames = 0;
    int frameCount = 0;
    cout << "START\n";
    bool paused = false;
    bool showOriginal = true;
    bool coloredPolynomials = true;
    filesystem::remove_all("video_output");
    filesystem::create_directory("video_output");

    while (window.isOpen())
    {
        // handle events
        for (auto event = sf::Event{}; window.pollEvent(event);) {
            if (event.type == sf::Event::Closed) {
                window.close();
            }

            if (event.type == sf::Event::KeyPressed) {
                switch (event.key.code) {
                    case (sf::Keyboard::Key::Escape):
                        paused = !paused;
                        break;
                    case (sf::Keyboard::Key::B):
                        showOriginal = !showOriginal;
                        break;
                    case (sf::Keyboard::Key::C):
                        coloredPolynomials = !coloredPolynomials;
                        break;
                    default:
                        break;
                }
            }
        }
        if (paused)
            continue;

        // handle frame buffer
        while (bufferedFrames < frameCount + BUFFER_SIZE && bufferedFrames < FRAMES) {
            ++bufferedFrames;
            string idStr = createFrameId(bufferedFrames);
            frameBuffer.emplace_back("resources/bad_apple_pngs/" + idStr + ".png");
            printf("load %d\n", bufferedFrames);
        }

        // DRAW
        window.clear(sf::Color::White);

        if (!frameBuffer.empty()) {
            // display source screen
            printf("frame %d\n", frameCount);
            Frame& frame = frameBuffer.front();
            videoTexture.draw(sf::Sprite(frameBuffer.front().texture));
            videoTexture.display();
            if (showOriginal)
                window.draw(sf::Sprite(videoTexture.getTexture()));

            // generate dest screen
            printf("Generating Polygons\n");
            vector<Polygon> islandPolygons = createIslandPolygons(frame.image);
            printf("Displaying Polygons\n");
            sf::CircleShape circle(2.f);
            circle.setPointCount(5);
            const sf::Color drawColors[] = {
                    sf::Color(255,0,0),
                    sf::Color(255,127,0),
                    sf::Color(0,255,0),
                    sf::Color(0,127,255),
                    sf::Color(0,0,255),
                    sf::Color(127,0,255),
            };
            printf("Drawing Barycentric Lagrangian Interpolation Curves");
            polyTexture.clear(sf::Color::Transparent);
            int pid = 0;
            for (Polygon& poly : islandPolygons) {
                if ((int)poly.points.size() / SAMPLE_SPACING < MIN_SAMPLE_POINTS)
                    continue;
                drawPolygon(polyTexture, poly, coloredPolynomials ? drawColors[pid % 6] : sf::Color::Black);
                ++pid;
            }
            polyTexture.display();
            sf::Sprite sprite(polyTexture.getTexture());
            window.draw(sprite);

            // save output image
            if (BG_OPACITY > 0) {
                outputTexture.draw(sf::Sprite(videoTexture.getTexture()));
                sf::RectangleShape fade;
                fade.setSize(sf::Vector2f{IMG_WIDTH, IMG_HEIGHT});
                fade.setFillColor(sf::Color(255, 255, 255, 255 - BG_OPACITY));
                outputTexture.draw(fade);
            } else {
                outputTexture.clear(sf::Color::White);
            }
            outputTexture.draw(sf::Sprite(polyTexture.getTexture()));
            outputTexture.display();
            outputTexture.getTexture().copyToImage().saveToFile("video_output/" + createFrameId(frameCount + 1) + ".png");

            // pop frame
            frameBuffer.pop_front();
        }

        // DRAW END
        window.display();
        ++frameCount;
    }
}

int main() {
    playVideo();
    return 0;
}