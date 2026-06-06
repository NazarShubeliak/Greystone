#include "astar.h"
#include <algorithm>
#include <queue>
#include <cstring>

extern Tile map[MAP_HEIGHT][MAP_WIDTH];

static int heuristic(int x1, int y1, int x2, int y2) {
    int dx = abs(x1 - x2);
    int dy = abs(y1 - y2);
    return 10 * (dx + dy) - 6 * std::min(dx, dy);
}

std::vector<SDL_Point> findPath(int startX, int startY, int endX, int endY) {
    std::vector<Node*> allNodes;

    static bool closedSet[MAP_HEIGHT][MAP_WIDTH];
    static Node* openMap[MAP_HEIGHT][MAP_WIDTH];
    memset(closedSet, 0, sizeof(closedSet));
    memset(openMap, 0, sizeof(openMap));

    auto cmp = [](const Node* a, const Node* b) { return a->f > b->f; };
    std::priority_queue<Node*, std::vector<Node*>, decltype(cmp)> openQueue(cmp);

    Node* start = new Node{startX, startY, 0, heuristic(startX, startY, endX, endY), 0, nullptr};
    start->f = start->h;
    allNodes.push_back(start);
    openQueue.push(start);
    openMap[startY][startX] = start;

    const int dx[] = {0, 0, 1, -1, 1, 1, -1, -1};
    const int dy[] = {1, -1, 0, 0, 1, -1, 1, -1};
    const int moveCost[] = {10, 10, 10, 10, 14, 14, 14, 14};

    std::vector<SDL_Point> result;

    while (!openQueue.empty()) {
        Node* current = openQueue.top();
        openQueue.pop();

        int cx = current->x, cy = current->y;
        if (closedSet[cy][cx]) continue;
        if (openMap[cy][cx] != current) continue;
        closedSet[cy][cx] = true;

        if (cx == endX && cy == endY) {
            Node* n = current;
            while (n) {
                result.push_back({n->x, n->y});
                n = n->parent;
            }
            std::reverse(result.begin(), result.end());
            break;
        }

        for (int i = 0; i < 8; i++) {
            int nx = cx + dx[i];
            int ny = cy + dy[i];

            if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;
            if (!map[ny][nx].walkable) continue;
            if (closedSet[ny][nx]) continue;

            int newG = current->g + moveCost[i];

            if (openMap[ny][nx] == nullptr || newG < openMap[ny][nx]->g) {
                int h = heuristic(nx, ny, endX, endY);
                Node* neighbor = new Node{nx, ny, newG, h, newG + h, current};
                allNodes.push_back(neighbor);
                openMap[ny][nx] = neighbor;
                openQueue.push(neighbor);
            }
        }
    }

    for (Node* n : allNodes) delete n;
    return result;
}
