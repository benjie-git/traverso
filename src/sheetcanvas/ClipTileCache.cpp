/*
Copyright (C) 2026 Ben Levitt

This file is part of Traverso

Traverso is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.
*/

#include "ClipTileCache.h"
#include <QElapsedTimer>

QElapsedTimer referenceTimer;


ClipTileCache::ClipTileCache()
{
    m_numTiles = 0;
    referenceTimer.start();
}


ClipTileCache& ctcache()
{
    static ClipTileCache clipTileCache;
    return clipTileCache;
}

quint128 ClipTileCache::hash(qint32 tileX, int height, int zoom, qreal devicePixelRatio, bool selected, bool hover)
{
    // generate a hash value / id for this tile.  It is unique per audio clip.
    quint128 hash = tileX;
    hash = (hash << 32) + height;
    hash = (hash << 32) + zoom;
    hash = (hash << 32) + quint32(((selected) ? 1 : 0) + ((hover) ? 2 : 0) + ((int(devicePixelRatio * 100) << 4)));
    return hash;
}

ClipTile* ClipTileCache::find(AudioClip* clip, quint128 hash)
{
    // find a cached clip for this hash, or return nullptr
    auto tileIt = m_tiles[clip].find(hash);
    if (tileIt != m_tiles[clip].end()) {
        ClipTile& tile = tileIt.value();
        m_tileRefs.remove(tile.lastAccessed);
        tile.lastAccessed = referenceTimer.nsecsElapsed();
        m_tileRefs[tile.lastAccessed] = TileRef(clip, hash);
        return &tile;
    }
    return nullptr;
}

ClipTile& ClipTileCache::insert(AudioClip* clip, quint128 hash, quint64 start, const QSize& size)
{
    // insert or replace a cached clip for this hash.
    ClipTile tile;

    tile.image = QImage(size, QImage::Format_ARGB32_Premultiplied);
    tile.image.fill(Qt::transparent);
    tile.startX = start;
    tile.lastAccessed = referenceTimer.nsecsElapsed();

    if (!m_tiles[clip].contains(hash)) {
        m_numTiles++;
        if (m_numTiles > clipTileCacheMaxTiles) {
            TileRef& ref = m_tileRefs.first();
            m_tiles[ref.clip].remove(ref.hash);
            m_tileRefs.remove(m_tileRefs.firstKey());
            m_numTiles--;
        }
    }
    else {
        m_tileRefs.remove(m_tiles[clip][hash].lastAccessed);
        m_numTiles--;
    }

    m_tileRefs[tile.lastAccessed] = TileRef(clip, hash);

    m_tiles[clip][hash] = tile;
    // printf("inserted: %lld, %lld, %d, %lld\n", m_tiles.size(), m_tiles[clip].size(), m_numTiles, m_tileRefs.size());
    return m_tiles[clip][hash];
}

void ClipTileCache::invalidate_clip(AudioClip* clip)
{
    // Invalidate all tiles for a clip

    auto clipIt = m_tiles.find(clip);
    if (clipIt != m_tiles.cend()) {
        m_numTiles -= clipIt.value().size();
        for (ClipTile& tile : clipIt.value()) {
            m_tileRefs.remove(tile.lastAccessed);
        }
        clipIt = m_tiles.erase(clipIt);
    }
    // printf("invalidate_clip: %lld, %lld, %d, %lld  --  ", m_tiles.size(), m_tiles[clip].size(), m_numTiles, m_tileRefs.size());
}

void ClipTileCache::invalidate_clip_range(AudioClip* clip, int zoom, qreal start, qreal end)
{
    // Invalidate all tiles for a clip that include any of the given range at the current zoom level,
    // and invalidate all tiles at other zoom levels.

    for (auto it = m_tiles[clip].cbegin(); it != m_tiles[clip].cend();) {
        const ClipTile& tile = it.value();
        if (tile.zoom == zoom) {
            if (end > tile.startX && start < tile.startX + ClipTileWidth) {
                if (m_tileRefs.remove(it.value().lastAccessed) == 0) printf("tile not removed 1\n");
                it = m_tiles[clip].erase(it);
                m_numTiles--;
            } else {
                ++it;
            }
        }
        else {
            if (m_tileRefs.remove(it.value().lastAccessed) == 0) printf("tile not removed 2\n");
            it = m_tiles[clip].erase(it);
            m_numTiles--;
        }
    }
    // printf("invalidate_clip_range: %lld, %lld, %d, %lld  --  ", m_tiles.size(), m_tiles[clip].size(), m_numTiles, m_tileRefs.size());
}

void ClipTileCache::invalidate_all()
{
    // nuke the whole cache (on a theme change, etc.)
    m_tiles.clear();
    m_tileRefs.clear();
    m_numTiles = 0;
}

