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

#ifndef CLIP_TILE_CACHE_H
#define CLIP_TILE_CACHE_H

#include <QHash>
#include <QMap>
#include <QImage>
#include <QtGlobal>
#include "AudioClip.h"
#include "AudioTrack.h"


const qint32 ClipTileWidth = 512;
const int clipTileCacheMaxTiles = 512;


// Used as a unique value per clip, to hash tiles
class TileHash
{
    public:
        TileHash() : tileX(0), height(0), zoom(0), devicePixelRatio(0), selected(0), hover(0) {}
        TileHash(qint32 tileX, int height, int zoom, int devicePixelRatio, bool selected, bool hover) : 
            tileX(tileX), height(height), zoom(zoom), devicePixelRatio(devicePixelRatio), selected(selected), hover(hover) {}
        TileHash(const TileHash& other) : tileX(other.tileX), height(other.height), zoom(other.zoom), devicePixelRatio(other.devicePixelRatio), selected(other.selected), hover(other.hover) {}

        qint32 tileX         : 32;
        int zoom             : 32;
        int height           : 16;
        int devicePixelRatio : 16;
        bool selected        : 1;
        bool hover           : 1;

        bool operator==(const TileHash& other) const {
            return (this->tileX == other.tileX) && (this->height == other.height) && (this->zoom == other.zoom) && 
                (this->devicePixelRatio == other.devicePixelRatio) && (this->selected == other.selected) && (this->hover == other.hover);
        }
        bool operator!=(const TileHash& other) const {
            return (this->tileX != other.tileX) || (this->zoom != other.zoom) || (this->height != other.height) ||
                (this->devicePixelRatio != other.devicePixelRatio) || (this->selected != other.selected) || (this->hover != other.hover);
        }
        friend inline size_t qHash(const TileHash& key, size_t seed = 0) noexcept {
            return qHashMulti(seed, key.tileX, key.height, key.zoom, key.devicePixelRatio, key.selected, key.hover);
        }
};

// Hold the image data for one tile of an AudioClipView's painted waveform
class ClipTile
{
    public:
        int zoom;
        quint64 startX;
        qint64 lastAccessed;
        QImage image;
};

// Reference a tile -- used to keep an ordered list of least recently used tiles, for culling the cache
class TileRef
{
    public:
        TileRef() : clip(0) {}
        TileRef(AudioClip *clip, TileHash hash) : clip(clip), hash(hash) {}
        TileRef(const TileRef& other) : clip(other.clip), hash(other.hash) {}
        AudioClip *clip;
        TileHash hash;
};


// Cache the AudioClipViews' painted waveforms, chunked into tiles.
// • Does not include the text, +6db, 0db lines, etc -- just the waveform.
// • Only cache zoom levels >=64 (non-microView).
// • Invalidating a clip or range means deleting those tiles, so that
//   they will get regenerated on the next update where they are each needed.
// • Store up to clipTileCacheMaxTiles tiles, and if we try to add one too many, first delete the oldest tile.
// • References to Tiles are kept in an ordered QMap to allow quickly finding the oldest to delete.
class ClipTileCache
{
public:
    ClipTileCache();

    ClipTile* find(AudioClip* clip, TileHash hash);
    ClipTile& insert(AudioClip* clip, TileHash hash, quint64 start, const QSize& size);
    void invalidate_clip(AudioClip* clip);
    void invalidate_clip_range(AudioClip* clip, int zoom, qreal left, qreal right);
    void invalidate_all();

private:
    QHash<AudioClip*, QHash<TileHash, ClipTile> > m_tiles;
    QMap<qint64, TileRef> m_tileRefs; // ordered by lastUpdated
    int m_numTiles;
};


// use this function to get the singleton ClipTileCache object
ClipTileCache& ctcache();


#endif
