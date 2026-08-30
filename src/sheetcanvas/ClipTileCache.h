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
#include <QImage>
#include <QtGlobal>
#include "AudioClip.h"
#include "AudioTrack.h"


const qint32 ClipTileWidth = 512;


// Hold the image data for one tile of an AudioClipView's painted waveform
class ClipTile
{
    public:
        int zoom;
        quint64 startX;
        QImage image;
};


// Cache the AudioClipViews' painted waveforms, chunked into tiles.
// • Does not include the text, +6db, 0db lines, etc -- just the waveform
// • Only cache zoom levels >=64 (non-microView)
// • Invalidating a clip or range means deleting those tiles, so that
//   they will get regenerated on the next update where they are each needed.
class ClipTileCache
{
public:
    quint128 hash(qint32 tileX, int height, int zoom, qreal devicePixelRatio, bool selected, bool hover);

    ClipTile* find(AudioClip* clip, quint128 hash);
    ClipTile& insert(AudioClip* clip, quint128 hash, quint64 start, const QSize& size);
    void invalidate_clip(AudioClip* clip);
    void invalidate_clip_range(AudioClip* clip, int zoom, qreal left, qreal right);
    void invalidate_all();

private:
    QHash<AudioClip*, QHash<quint128, ClipTile> > m_tiles;
};


// use this function to get the singleton ClipTileCache object
ClipTileCache& ctcache();


#endif
