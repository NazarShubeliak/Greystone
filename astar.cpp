#include "astar.h"
#include <algorithm>

extern Tile map[MAP_HEIGHT][MAP_WIDTH];

int heuristic(int x1, int y1, int x2, int y2) {
    return abs(x1 - x2) + abs(y1 - y2);
}

std::vector<SDL_Point> findPath(int startX, int startY, int endX, int endY) {
    std::vector<Node*> openList;
    std::vector<Node*> closedList;

    Node* startNode = new Node{startX, startY, 0, 0, 0, nullptr};
    startNode->h = heuristic(startX, startY, endX, endY);
    startNode->f = startNode->h;
    openList.push_back(startNode);

    while (!openList.empty()) {
        Node* current = openList[0];
        for (Node* node : openList) {
            if (node->f < current->f) {
                current = node;
            }
        }

        if (current->x == endX && current->y == endY) {
            std::vector<SDL_Point> path;
            while (current != nullptr) {
                path.push_back({current->x, current->y});
                current = current->parent;
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        openList.erase(std::remove(openList.begin(), openList.end(), current), openList.end());
        closedList.push_back(current);

        int dx[] = {0, 0, 1, -1, 1, 1, -1, -1};
        int dy[] = {1, -1, 0, 0, 1, -1, 1, -1};

        for (int i = 0; i < 8; i++) {
            int nx = current->x + dx[i];
            int ny = current->y + dy[i];

            if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;
            if (!map[ny][nx].walkable) continue;

            bool inClosed = false;
            for (Node* node : closedList) {
                if (node->x == nx && node->y == ny) {
                    inClosed = true;
                    break;
                }
            }
            if (inClosed) continue;

            int newG = current->g + 1;

            Node* existingNode = nullptr;
            for (Node* node : openList) {
                if (node->x == nx && node->y == ny) {
                    existingNode = node;
                    break;
                }
            }

            if (existingNode == nullptr) {
                Node* neighbor = new Node{nx, ny, newG, 0, 0, current};
                neighbor->h = heuristic(nx, ny, endX, endY);
                neighbor->f = neighbor->g + neighbor->h;
                openList.push_back(neighbor);
            } else if (newG < existingNode->g) {
                existingNode->g = newG;
                existingNode->f = existingNode->g + existingNode->h;
                existingNode->parent = current;
            }
        }
    }

    return {};
}