/*
    Copyright (C) 2012-2013 by Michael Kristofik <kristo605@gmail.com>
    Part of the libsdl-demos project.
 
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2
    or at your option any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY.
 
    See the COPYING.txt file for more details.
*/
#include "SDL.h"
#include "SDL_image.h"

#include <algorithm>
#include <bitset>
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

using SdlSurface = std::shared_ptr<SDL_Surface>;
using Point = std::pair<Sint16, Sint16>;

// Experiment with Apps Hungarian notation:
// a = hex number as an array index
// h = hex coordinate
// p = pixel coordinate
// r = region number

const Sint16 hexSize = 72;
const Sint16 hMapWidth = 16;
const Sint16 hMapHeight = 9;
const Sint16 hMapSize = hMapWidth * hMapHeight;
const int numRegions = 18;
const int numTerrains = 6;
const Point hInvalid = {-1, -1};

SDL_Surface *screen = nullptr;

SdlSurface make_surface(SDL_Surface *surf)
{
    return SdlSurface(surf, SDL_FreeSurface);
}

std::ostream & operator<<(std::ostream &os, const Point &p)
{
    os << '(' << p.first << ',' << p.second << ')';
    return os;
}

Point hexFromAry(int aIndex)
{
    return {aIndex % hMapWidth, aIndex / hMapWidth};
}

int aryFromHex(const Point &hex)
{
    return hex.second * hMapWidth + hex.first;
}

int aryFromHex(Sint16 hx, Sint16 hy)
{
    return hy * hMapWidth + hx;
}

Point hexRandom()
{
    static std::minstd_rand gen(static_cast<unsigned int>(std::time(nullptr)));
    static std::uniform_int_distribution<Sint16> dist(0, hMapWidth * hMapHeight - 1);
    Sint16 aRand = dist(gen);
    return hexFromAry(aRand);
}

// source: Battle for Wesnoth, distance_between() in map_location.cpp.
Sint16 hexDist(const Point &h1, const Point &h2)
{
    if (h1 == hInvalid || h2 == hInvalid) {
        return std::numeric_limits<Sint16>::max();
    }

    Sint16 dx = abs(h1.first - h2.first);
    Sint16 dy = abs(h1.second - h2.second);

    // Since the x-axis of the hex grid is staggered, we need to add a step in
    // certain cases.
    Sint16 vPenalty = 0;
    if ((h1.second < h2.second && h1.first % 2 == 0 && h2.first % 2 == 1) ||
        (h1.second > h2.second && h1.first % 2 == 1 && h2.first % 2 == 0)) {
        vPenalty = 1;
    }

    return std::max<Sint16>(dx, dy + vPenalty + dx / 2);
}

// Return the array index of a neighbor tile in the given direction (0 = north,
// 1 = northeast, etc.).  Return -1 if no tile exists in that direction.
int aryGetNeighbor(int aIndex, int dir)
{
    assert(dir >= 0 && dir < 6);

    // North, below the top row.
    if (dir == 0 && aIndex >= hMapWidth) {
        return aIndex - hMapWidth;
    }
    // Northeast, not in the right column.
    else if (dir == 1 && (aIndex % hMapWidth < hMapWidth - 1)) {
        if (aIndex % 2 == 0) {
            if (aIndex >= hMapWidth) {
                return aIndex - hMapWidth + 1;
            }
        }
        else {
            return aIndex + 1;
        }
    }
    // Southeast, not in the right column.
    else if (dir == 2 && (aIndex % hMapWidth < hMapWidth - 1)) {
        if (aIndex % 2 == 0) {
            return aIndex + 1;
        }
        else {
            if (aIndex < hMapSize - hMapWidth) {
                return aIndex + hMapWidth + 1;
            }
        }
    }
    // South, above the bottom row.
    else if (dir == 3 && (aIndex < hMapSize - hMapWidth)) {
        return aIndex + hMapWidth;
    }
    // Southwest, not in the left column.
    else if (dir == 4 && (aIndex % hMapWidth > 0)) {
        if (aIndex % 2 == 0) {
            return aIndex - 1;
        }
        else {
            if (aIndex < hMapSize - hMapWidth) {
                return aIndex + hMapWidth - 1;
            }
        }
    }
    // Northwest, not in the left column.
    else if (dir == 5 && (aIndex % hMapWidth > 0)) {
        if (aIndex % 2 == 0) {
            if (aIndex >= hMapWidth) {
                return aIndex - hMapWidth - 1;
            }
        }
        else {
            return aIndex - 1;
        }
    }

    return -1;
}

// Compute all neighbors of a given tile.  Result might have size < 6.
std::vector<int> aryNeighbors(int aIndex)
{
    std::vector<int> av;

    for (int dir = 0; dir < 6; ++dir) {
        auto aNeighbor = aryGetNeighbor(aIndex, dir);
        if (aNeighbor != -1) {
            av.push_back(aNeighbor);
        }
    }

    return av;
}

// Same as aryNeighbors() but with hex coordinates instead of array indexes.
std::vector<Point> hexNeighbors(const Point &hex)
{
    std::vector<Point> hv;

    auto av = aryNeighbors(aryFromHex(hex));
    for (auto aNeighbor : av) {
        hv.push_back(hexFromAry(aNeighbor));
    }

    return hv;
}

// Return the closest region to the given hex (hx,hy).
int rFindClosest(Sint16 hx, Sint16 hy, const std::vector<Point> &hCenters)
{
    int rClosest = -1;
    Sint16 bestSoFar = std::numeric_limits<Sint16>::max();

    for (int i = 0; i < numRegions; ++i) {
        Sint16 dist = hexDist(Point(hx, hy), hCenters[i]);
        if (dist < bestSoFar) {
            rClosest = i;
            bestSoFar = dist;
        }
    }

    return rClosest;
}

// Compute the centers of mass of each region.
std::vector<Point> hGetCenters(const std::vector<int> &regions)
{
    std::vector<Point> hexSums(numRegions);
    std::vector<int> numHexes(numRegions);
    std::vector<Point> hCenters(numRegions, hInvalid);

    assert(regions.size() == unsigned(hMapSize));
    for (Sint16 hx = 0; hx < hMapWidth; ++hx) {
        for (Sint16 hy = 0; hy < hMapHeight; ++hy) {
            int region = regions[hy * hMapWidth + hx];
            assert(region >= 0 && region < numRegions);

            auto &hs = hexSums[region];
            hs.first += hx;
            hs.second += hy;
            ++numHexes[region];
        }
    }

    for (int r = 0; r < numRegions; ++r) {
        // The voronoi algorithm sometimes leads to regions being "absorbed" by
        // their neighbors, leaving no hexes left.  Leave the default (invalid)
        // center hex in place for such a region.
        if (numHexes[r] > 0) {
            auto &hc = hCenters[r];
            auto &hs = hexSums[r];
            hc.first = hs.first / numHexes[r];
            hc.second = hs.second / numHexes[r];
        }
    }

    return hCenters;
}

// Use a voronoi diagram to generate a random set of regions.
std::vector<int> rGenerate()
{
    // Start with a set of random center points.  Don't worry if there are
    // duplicates.
    std::vector<Point> hCenters;
    for (int r = 0; r < numRegions; ++r) {
        hCenters.push_back(hexRandom());
    }

    std::vector<int> regions(hMapSize);
    for (int i = 0; i < 4; ++i) {
        // Find the closest center to each hex on the map, number those
        // regions.
        for (Sint16 hx = 0; hx < hMapWidth; ++hx) {
            for (Sint16 hy = 0; hy < hMapHeight; ++hy) {
                regions[hy * hMapWidth + hx] = rFindClosest(hx, hy, hCenters);
            }
        }

        hCenters = hGetCenters(regions);
        // Repeat this process to make more regular-looking regions.
    }

    // Assign each hex to its final region.
    for (Sint16 hx = 0; hx < hMapWidth; ++hx) {
        for (Sint16 hy = 0; hy < hMapHeight; ++hy) {
            regions[hy * hMapWidth + hx] = rFindClosest(hx, hy, hCenters);
        }
    }

    return regions;
}

// Construct an adjacency list for each region.
std::vector<std::vector<int>> regionNeighbors(const std::vector<int> &regions)
{
    assert(regions.size() == unsigned(hMapSize));

    std::vector<std::vector<int>> ret(numRegions);
    for (int i = 0; i < hMapSize; ++i) {
        int reg = regions[i];
        assert(reg >= 0 && reg < numRegions);

        for (const auto &an : aryNeighbors(i)) {
            int rNeighbor = regions[an];
            // If an adjacent hex is in a different region and we haven't
            // already recorded that region as a neighbor, save it.
            if (rNeighbor != reg && find(std::begin(ret[reg]),
                                         std::end(ret[reg]),
                                         rNeighbor) == std::end(ret[reg])) {
                ret[reg].push_back(rNeighbor);
            }
        }
    }

    return ret;
}

// Assign a terrain type to each region using the given adjacency list.
std::vector<int> assignTerrain(const std::vector<std::vector<int>> &adj)
{
    std::vector<int> terrain(numRegions, -1);

    // Greedy coloring.  Each region gets a different terrain from its
    // neighbors, using the lowest number available.
    for (int r = 0; r < numRegions; ++r) {
        // For each neighboring region, save which terrains have been assigned.
        std::bitset<numTerrains> assignedTerrains;
        for (auto rNeighbor : adj[r]) {
            assert(rNeighbor >= 0 && rNeighbor < numRegions);
            if (terrain[rNeighbor] > -1) {
                assignedTerrains[terrain[rNeighbor]] = true;
            }
        }
        if (!assignedTerrains.all()) {
            for (int t = 0; t < numTerrains; ++t) {
                if (!assignedTerrains[t]) {
                    terrain[r] = t;
                    break;
                }
            }
        }
        else {
            terrain[r] = 0;
        }
    }

    return terrain;
}

void sdlBlit(const SdlSurface &surf, Sint16 px, Sint16 py)
{
    assert(screen != nullptr);
    SDL_Rect dest = {px, py, 0, 0};
    if (SDL_BlitSurface(surf.get(), nullptr, screen, &dest) < 0) {
        std::cerr << "Warning: error drawing to screen: " << SDL_GetError();
    }
}

SdlSurface sdlLoadImage(const char *filename)
{
    auto temp = make_surface(IMG_Load(filename));
    if (!temp) {
        std::cerr << "Error loading image " << filename
            << "\n    " << IMG_GetError() << '\n';
        return temp;
    }
    auto surf = make_surface(SDL_DisplayFormatAlpha(temp.get()));
    if (!surf) {
        std::cerr << "Error converting to display format: "
            << "\n    " << IMG_GetError() << '\n';
    }

    return surf;
}

extern "C" int SDL_main(int, char **)  // 2-arg form is required by SDL
{
    if (SDL_Init(SDL_INIT_VIDEO) == -1) {
        std::cerr << "Error initializing SDL: " << SDL_GetError();
        return EXIT_FAILURE;
    }
    atexit(SDL_Quit);

    if (IMG_Init(IMG_INIT_PNG) < 0) {
        std::cerr << "Error initializing SDL_image: " << IMG_GetError();
        return EXIT_FAILURE;
    }
    atexit(IMG_Quit);

    // Have to do this prior to SetVideoMode.
    auto icon = make_surface(IMG_Load("../img/icon.png"));
    if (icon != nullptr) {
        SDL_WM_SetIcon(icon.get(), nullptr);
    }
    else {
        std::cerr << "Warning: error loading icon file: " << IMG_GetError();
    }

    screen = SDL_SetVideoMode(882, 684, 0, SDL_SWSURFACE);
    if (screen == nullptr) {
        std::cerr << "Error setting video mode: " << SDL_GetError();
        return EXIT_FAILURE;    
    }
    SDL_WM_SetCaption("Random Map Test", "");

    std::vector<SdlSurface> tiles;
    tiles.push_back(sdlLoadImage("../img/grass.png"));
    tiles.push_back(sdlLoadImage("../img/dirt.png"));
    tiles.push_back(sdlLoadImage("../img/desert.png"));
    tiles.push_back(sdlLoadImage("../img/water.png"));
    tiles.push_back(sdlLoadImage("../img/swamp.png"));
    tiles.push_back(sdlLoadImage("../img/snow.png"));

    std::vector<SdlSurface> edges;
    edges.push_back(sdlLoadImage("../img/beach-n.png"));
    edges.push_back(sdlLoadImage("../img/beach-ne.png"));
    edges.push_back(sdlLoadImage("../img/beach-se.png"));
    edges.push_back(sdlLoadImage("../img/beach-s.png"));
    edges.push_back(sdlLoadImage("../img/beach-sw.png"));
    edges.push_back(sdlLoadImage("../img/beach-nw.png"));

    auto regions = rGenerate();
    auto adjacencyList = regionNeighbors(regions);
    auto terrain = assignTerrain(adjacencyList);

    // Display even-numbered columns.
    for (Sint16 hx = 0; hx < hMapWidth; hx += 2) {
        for (Sint16 hy = 0; hy < hMapHeight; ++hy) {
            auto aPos = aryFromHex(hx, hy);
            auto terrainIndex = terrain[regions[aPos]];
            sdlBlit(tiles[terrainIndex], hx * hexSize * 0.75, hy * hexSize);
            if (terrainIndex == 3) {  // water
                for (auto dir = 0; dir < 6; ++dir) {
                    auto aNeighbor = aryGetNeighbor(aPos, dir);
                    if (aNeighbor >= 0 && terrain[regions[aNeighbor]] != 3) {
                        sdlBlit(edges[dir], hx * hexSize * 0.75, hy * hexSize);
                    }
                }
            }
            if (terrainIndex != 3) {  // not water
                for (auto dir = 0; dir < 6; ++dir) {
                    auto aNeighbor = aryGetNeighbor(aPos, dir);
                    if (aNeighbor >= 0 && terrain[regions[aNeighbor]] == 3) {
                        sdlBlit(edges[dir], hx * hexSize * 0.75, hy * hexSize);
                    }
                }
            }
        }
    }

    // Display odd-numbered columns.
    for (Sint16 hx = 1; hx < hMapWidth; hx += 2) {
        for (Sint16 hy = 0; hy < hMapHeight; ++hy) {
            auto aPos = aryFromHex(hx, hy);
            auto terrainIndex = terrain[regions[aPos]];
            sdlBlit(tiles[terrainIndex], hx * hexSize * 0.75, (hy+0.5) * hexSize);
            if (terrainIndex == 3) {  // water
                for (auto dir = 0; dir < 6; ++dir) {
                    auto aNeighbor = aryGetNeighbor(aPos, dir);
                    if (aNeighbor >= 0 && terrain[regions[aNeighbor]] != 3) {
                        sdlBlit(edges[dir], hx * hexSize * 0.75, (hy+0.5) * hexSize);
                    }
                }
            }
            if (terrainIndex != 3) {  // not water
                for (auto dir = 0; dir < 6; ++dir) {
                    auto aNeighbor = aryGetNeighbor(aPos, dir);
                    if (aNeighbor >= 0 && terrain[regions[aNeighbor]] == 3) {
                        sdlBlit(edges[dir], hx * hexSize * 0.75, (hy+0.5) * hexSize);
                    }
                }
            }
        }
    }
    // Overdraw so we don't get jagged edges, copying from neighboring hexes.

    // left edge, hx = -1
    for (Sint16 hy = -1; hy < hMapHeight; ++hy) {
        auto aNeighbor = aryFromHex(0, std::min(hy + 1, hMapHeight - 1));
        auto terrainIndex = terrain[regions[aNeighbor]];
        sdlBlit(tiles[terrainIndex], -0.75 * hexSize, (hy+0.5) * hexSize);
    }
    // top edge, hy = -1
    for (Sint16 hx = 1; hx < hMapWidth; hx += 2) {
        auto aNeighbor = aryFromHex(hx, 0);
        auto terrainIndex = terrain[regions[aNeighbor]];
        sdlBlit(tiles[terrainIndex], hx * hexSize * 0.75, -0.5 * hexSize);
    }
    // right edge, hx = hMapWidth
    for (Sint16 hy = 0; hy < hMapHeight + 1; ++hy) {
        auto aNeighbor = aryFromHex(hMapWidth - 1,
                                    std::min<int>(hy, hMapHeight - 1));
        auto terrainIndex = terrain[regions[aNeighbor]];
        sdlBlit(tiles[terrainIndex], hMapWidth * hexSize * 0.75, hy * hexSize);
    }
    // bottom edge, hy = hMapHeight
    for (Sint16 hx = 0; hx < hMapWidth; hx += 2) {
        auto aNeighbor = aryFromHex(hx, hMapHeight - 1);
        auto terrainIndex = terrain[regions[aNeighbor]];
        sdlBlit(tiles[terrainIndex], hx * hexSize * 0.75, hMapHeight * hexSize);
    }

    SDL_UpdateRect(screen, 0, 0, 0, 0);

    bool isDone = false;
    SDL_Event event;
    while (!isDone) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                isDone = true;
            }
        }
        SDL_Delay(1);
    }

    // TODO: unit tests?
    assert(hexDist({1, 1}, {2, 2}) == 1);
    assert(hexDist({4, 4}, {3, 3}) == 1);
    assert(hexDist({1, 1}, {3, 3}) == 3);
    assert(hexDist({7, 7}, {5, 5}) == 3);

    for (int i = 0; i < 2; ++i) {
        auto hex = hexRandom();
        auto a = aryFromHex(hex);
        auto aryN = aryNeighbors(a);
        auto hexN = hexNeighbors(hex);
        assert(aryN.size() == hexN.size());
        std::cout << hex << " neighbors are ";
        for (unsigned int i = 0; i < hexN.size(); ++i) {
            std::cout << hexN[i] << ',';
            assert(aryFromHex(hexN[i]) == aryN[i]);
        }
        std::cout << '\n';
    }

    int count = 0;
    for (const auto &hNeighbors : adjacencyList) {
        std::cout << count << ": ";
        for_each(std::begin(hNeighbors), std::end(hNeighbors),
                 [] (int hn) { std::cout << hn << ','; });
        std::cout << '\n';
        ++count;
    }

    return EXIT_SUCCESS;
}