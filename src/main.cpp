#include <SFML/Graphics.hpp>
#include <iostream>
#include <utility>
#include <vector>
#include <deque>
#include <cmath>
#include <map>
#include <set>
#include <complex>
#define CNum complex<double>
#define SINGLE_POLYNOMIAL false

/* CMAKE COMMANDS:
 * cmake -S . -B build
 * cmake --build build --config Release */

const int IMG_HEIGHT = 720;
const int IMG_WIDTH = 960;
const int IMG_SIZE = IMG_WIDTH * IMG_HEIGHT;
const int MIN_ISLAND_SIZE = 10;
const int SAMPLE_SPACING = 10;
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

        // TODO: Have Polygon's start point be as close to center of mass to prevent wobbles
    }

    // get point p proportional along polygon, p in [0, 1]
    pair<double,double> getPoint(double p) {
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
            int dir = 0;
            int px = x;
            int py = y;
            set<long long> edges;
            while (true) {
                // try all 8 directions
                bool searching = true;
                for (int i = 0; i < 8 && searching; ++i) {
                    dir = (dir + 1) & 0b111;
                    int qx = px + offsets[dir].first;
                    int qy = py + offsets[dir].second;
                    if (qx < 0 || qy < 0 || qx >= IMG_WIDTH || qy >= IMG_HEIGHT)
                        continue;
                    if (visited[qy][qx])
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
            printf("PATH %d size=%d\n", (int)islandPolygons.size() + 1, path.size());
            for (auto& p : path)
                visited[p.second][p.first] |= 2;
            islandPolygons.emplace_back(move(path));
        }
    }
    return islandPolygons;
}

void playVideo()
{
    auto window = sf::RenderWindow{ { 1920u, 1080u }, "CMake SFML Project" };
    window.setFramerateLimit(30);
    const int FRAMES = 6572;
    const int BUFFER_SIZE = 20;
    deque<Frame> frameBuffer;
    int bufferedFrames = 0;
    int frameCount = 0;
    sf::Sprite screen;
    sf::Image screenImage;
    cout << "START\n";
    bool paused = false;
    bool showOriginal = true;

    while (window.isOpen())
    {
        // handle events
        for (auto event = sf::Event{}; window.pollEvent(event);) {
            if (event.type == sf::Event::Closed) {
                window.close();
            }

            if (event.type == sf::Event::KeyPressed) {
                if (event.key.code == sf::Keyboard::Key::Escape)
                    paused = !paused;
                if (event.key.code == sf::Keyboard::Key::B)
                    showOriginal = !showOriginal;
            }
        }
        if (paused)
            continue;

        // handle frame buffer
        while (bufferedFrames < frameCount + BUFFER_SIZE && bufferedFrames < FRAMES) {
            ++bufferedFrames;
            string idStr;
            int id = bufferedFrames;
            for (int j = 0; j < 5; ++j) {
                idStr.push_back((char)('0' + id % 10));
                id /= 10;
            }
            reverse(idStr.begin(), idStr.end());
            frameBuffer.emplace_back("resources/bad_apple_pngs/" + idStr + ".png");
            printf("load %d\n", bufferedFrames);
        }

        // DRAW
        window.clear(sf::Color::White);

        if (!frameBuffer.empty()) {
            // display source screen
            printf("frame %d\n", frameCount);
            Frame& frame = frameBuffer.front();
            if (showOriginal) {
                screen.setTexture(frameBuffer.front().texture);
                window.draw(screen);
            }

            // generate dest screen
            printf("Generating Polygons\n");
            vector<Polygon> islandPolygons = createIslandPolygons(frame.image);
            printf("Displaying Polygons\n");
            sf::CircleShape circle(2.f);
            circle.setPointCount(5);
            /*
            circle.setFillColor(sf::Color::Blue);
            for (Polygon& poly : islandPolygons) {
                for (auto &p: poly.points) {
                    circle.setPosition(p.first, p.second);
                    window.draw(circle);
                }
            }
             */
            int pid = 0;
            const sf::Color drawColors[] = { sf::Color::Red, sf::Color::Yellow, sf::Color::Green, sf::Color::Cyan, sf::Color::Blue, sf::Color::Magenta };
            vector<pair<int,int>> completePath;
#if SINGLE_POLYNOMIAL
            if (!islandPolygons.empty()) {
                for (Polygon &poly: islandPolygons)
                    for (auto &p: poly.points)
                        completePath.emplace_back(p);
                Polygon completePoly(completePath);
                islandPolygons.clear();
                islandPolygons.push_back(completePoly);
            }
#endif

            for (Polygon& poly : islandPolygons) {
                if (poly.points.size() < MIN_ISLAND_SIZE)
                    continue;
                int sample_points = max(3, (int)poly.points.size() / SAMPLE_SPACING);
                vector<double> xVec = BLI::chebyshev2(sample_points);
                vector<CNum> yVec;
                yVec.reserve(xVec.size());
                circle.setFillColor(drawColors[pid % 6]);
                for (double& x : xVec) {
                    pair<double, double> point = poly.getPoint(x * 0.5 + 0.5);
                    yVec.emplace_back(point.first, point.second);
                    circle.setPosition((float)point.first, (float)point.second);
                    window.draw(circle);
                }
                int eval_points = max(3, (int)poly.points.size() * 2);
                for (int i = 0; i < eval_points; ++i) {
                    double x = (double) i * 2 / eval_points - 1;
                    CNum point = BLI::eval(yVec, x);
                    circle.setPosition((float)point.real(), (float)point.imag());
                    window.draw(circle);
                }
                ++pid;
            }

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