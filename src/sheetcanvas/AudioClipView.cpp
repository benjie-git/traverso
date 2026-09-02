/*
Copyright (C) 2005-2009 Remon Sijrier

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

#include <QPainter>
#include <QFont>
#include <QGraphicsSimpleTextItem>

#include "AudioClipView.h"
#include "SheetView.h"
#include "AudioTrackView.h"
#include "TTrackLaneView.h"
#include "FadeCurveView.h"
#include "CurveView.h"

#include "AudioClip.h"
#include "ReadSource.h"
#include "TInputEventDispatcher.h"
#include "ContextPointer.h"
#include "Sheet.h"
#include "ResourcesManager.h"
#include "ProjectManager.h"
#include "Peak.h"
#include "Information.h"
#include "Themer.h"
#include "TConfig.h"
#include <FadeCurve.h>
#include <Curve.h>
#include "TMainWindow.h"
#include "PluginChain.h"
#include "Mixer.h"
#include "ClipTileCache.h"

#include <QFileDialog>
#include <QLinearGradient>
#include <cmath>
#include "dialogs/AudioClipEditDialog.h"
#include "Fade.h"
#include "AudioDevice.h"

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


AudioClipView::AudioClipView(SheetView* sv, AudioTrackView* parent, AudioClip* clip )
    : ViewItem(parent->get_primary_lane_view(), clip)
    , m_tv(parent)
    , m_clip(clip)
    , m_gainCurveView(nullptr)
{
    PENTERCONS;

    setZValue(parent->zValue() + 1);
    setFlags(QGraphicsItem::ItemUsesExtendedStyleOption);

    m_sv = sv;
    m_sheet = m_clip->get_sheet();

    load_theme_data();

    m_waitingForPeaks = false;
    m_progress = 0;

    if (FadeCurve* curve = m_clip->get_fade_in()) {
        add_new_fade_curve_view(curve);
    }
    if (FadeCurve* curve = m_clip->get_fade_out()) {
        add_new_fade_curve_view(curve);
    }

    m_gainCurveView = new CurveView(m_sv, this, m_clip->get_plugin_chain()->get_fader()->get_curve());
    // CurveViews don't 'get' their start offset, it's only a property for AudioClips.
    // So to be sure the CurveNodeViews start offset get updated as well,
    // we call curveviews calculate_bounding_rect() function!
    m_gainCurveView->set_start_offset(m_clip->get_source_start_location());
    connect(m_gainCurveView, SIGNAL(curveModified()), m_sv, SLOT(stop_follow_play_head()));
    connect(m_gainCurveView, SIGNAL(curveUpdated(int, int)), this, SLOT(invalidate_tiles_range(int, int)));

    connect(m_clip, SIGNAL(muteChanged()), this, SLOT(invalidate_clip_tiles()));
    connect(m_clip, SIGNAL(edgeMoved(bool)), this, SLOT(invalidate_edge_tiles(bool)));
    connect(m_clip, SIGNAL(stateChanged()), this, SLOT(clip_state_changed()));
    connect(m_clip, SIGNAL(activeContextChanged()), this, SLOT(active_context_changed()));
    connect(m_clip, SIGNAL(lockChanged()), this, SLOT(repaint()));
    connect(m_clip, SIGNAL(fadeAdded(FadeCurve*)), this, SLOT(add_new_fade_curve_view( FadeCurve*)));
    connect(m_clip, SIGNAL(fadeRemoved(FadeCurve*)), this, SLOT(remove_fade_curve_view( FadeCurve*)));
    connect(m_clip, SIGNAL(positionChanged()), this, SLOT(position_changed()));

    if (m_clip->recording_state() == AudioClip::RECORDING) {
        start_recording();
        connect(m_clip, SIGNAL(recordingFinished(AudioClip*)), this, SLOT(finish_recording()));
    }
}

AudioClipView::~ AudioClipView()
{
    PENTERDES;
}

void AudioClipView::paint(QPainter* painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    PENTER2;
    Q_UNUSED(widget);

    //        printf("AudioClipView:: %s PAINT :: exposed rect is: x=%f, y=%f,? w=%f, h=%f\n", QS_C(m_clip->get_name()), option->exposedRect.x(), option->exposedRect.y(), option->exposedRect.width(), option->exposedRect.height());

    //	printf("x, w %f, %f\n", option->exposedRect.x(), option->exposedRect.width());
    qreal xstart = option->exposedRect.x()-1;
    qreal pixelcount = option->exposedRect.width()+2;
    if (pixelcount < 3) {
        // apparently this function can be called with no pixelcount to go with
        // so return here nothing to be done
        return;
    }
    painter->save();
    painter->setClipRect(m_boundingRect);

    QRectF fillRect = QRectF(xstart+1, 0.0, pixelcount-1, qreal(m_height));

    if (m_clip->is_readsource_invalid()) {
        painter->fillRect(fillRect, themer()->get_color("AudioClip:invalidreadsource"));
        draw_clipinfo_area(painter, xstart);
        painter->setPen(themer()->get_color("AudioClip:contour"));
        painter->drawRect(xstart, 0, pixelcount, m_height - 1);
        painter->setPen(Qt::black);
        painter->setFont( themer()->get_font("AudioClip:fontscale:title") );
        painter->drawText(30, 0, 300, m_height, Qt::AlignVCenter, tr("Click to reset AudioFile !"));
        painter->restore();
        return;
    }

    bool mousehover = m_clip->has_active_context() || m_clip->is_moving();

    if (m_drawbackground) {
        if (m_clip->recording_state() == AudioClip::RECORDING) {
            painter->fillRect(fillRect, m_brushBgRecording);
        } else {
            if (m_clip->is_selected()) {
                if (mousehover) painter->fillRect(fillRect, m_brushBgSelectedHover);
                else            painter->fillRect(fillRect, m_brushBgSelected);
            } else if (m_clip->is_muted()) {
                if (mousehover) painter->fillRect(fillRect, m_brushBgMutedHover);
                else            painter->fillRect(fillRect, m_brushBgMuted);
            } else {
                if (mousehover) painter->fillRect(fillRect, m_brushBgHover);
                else            painter->fillRect(fillRect, m_brushBg);
            }
        }
    }

    if (m_clip->is_muted()) {
        m_waveBrush = m_brushFgMuted;
    } else {
        if (m_sheet->get_mode() == Sheet::EDIT) {
            if (mousehover) m_waveBrush = m_brushFgHover;
            else            m_waveBrush = m_brushFg;
        } else {
            if (mousehover) m_waveBrush = m_brushFgEditHover;
            else            m_waveBrush = m_brushFgEdit;
        }
    }

    int channels = m_clip->get_channel_count();

    if (channels > 0) {
        if (m_waitingForPeaks) {
            PMESG("Waiting for peaks!");
            // Hmm, do we paint here something?
            // Progress info, I think so....
            painter->setPen(Qt::black);
            QRect r(10, 0, 150, m_height);
            painter->setFont( themer()->get_font("AudioClip:fontscale:title") );
            QString si;
            si.setNum((int)m_progress);
            if (m_progress == 100) m_progress = 0;
            QString buildProcess = "Building Peaks: " + si + "%";
            painter->drawText(r, Qt::AlignVCenter, buildProcess);

        } else if (m_clip->recording_state() == AudioClip::NO_RECORDING) {
            //                        PROFILE_START;
            draw_peaks(painter, option->exposedRect.x(), pixelcount-1);
            //                        PROFILE_END("draw peaks");
        }
    }

    draw_clipinfo_area(painter, xstart);

    // Draw the db lines at 0 and -6 db
    if (m_drawDbGrid) {
        draw_db_lines(painter, xstart, pixelcount);
    }

    // Draw the contour
    painter->setPen(themer()->get_color("AudioClip:contour"));
    painter->drawRect(m_boundingRect.adjusted(0, 0, -0.5, -1));

    // Paint a pixmap if the clip is locked
    if (m_clip->is_locked()) {
        int center = (int)(m_clip->get_length() / (2 * m_sv->timeref_scalefactor));
        painter->drawPixmap(center - 8, m_height - 20, find_pixmap(":/lock"));
    }

    painter->restore();
}

void AudioClipView::draw_peaks(QPainter* p, qreal xstart, int pixelcount)
{
    PENTER4;

    // Draw the necessary tiles, usually most of which are already cached in memory.
    const qreal sourceStartPixels = double(m_clip->get_source_start_location() / m_sv->timeref_scalefactor);
    qreal tileStart = xstart + sourceStartPixels;
    int remaining = pixelcount;
    while (remaining > 0) {
        const qint64 tileX = qFloor(tileStart / ClipTileWidth) * ClipTileWidth;
        const int tileOffset = qMax(0, int(tileStart - tileX));
        const int tilePixels = qMin(remaining, ClipTileWidth - tileOffset);
        paint_tile(p, tileStart - sourceStartPixels, tilePixels);
        tileStart += tilePixels;
        remaining -= tilePixels;
    }
}

void AudioClipView::paint_tile(QPainter* painter, qreal xstart, int pixelcount)
{
    if (!painter || !m_clip->get_peak() || pixelcount <= 0 || m_height <= 0) {
        return;
    }

    const uint channels = m_clip->get_channel_count();
    if (!channels) {
        return;
    }

    constexpr int tilePadding = 4;
    const qint64 timeRefScale = m_sv->timeref_scalefactor;
    const qreal sourceStartPixels = double(m_clip->get_source_start_location() / timeRefScale);
    const qint64 tileX = qFloor((xstart + sourceStartPixels) / ClipTileWidth) * ClipTileWidth;
    const qreal renderStart = tileX - sourceStartPixels - tilePadding;
    const int width = ClipTileWidth + tilePadding * 2;
    const qreal zoom = m_sheet->get_hzoom();
    qreal devicePixelRatio = painter->device()->devicePixelRatioF();
    const bool microView = zoom < 64;

    // The hash/ID of this tile in the cache
    TileHash hash = TileHash(tileX, m_height, zoom, int(devicePixelRatio *100), m_clip->is_selected(), 
        m_clip->has_active_context() || m_clip->is_moving());
    ClipTile* cachedTile = microView ? nullptr : ctcache().find(m_clip, hash);
    QImage cachedImage = (cachedTile) ? cachedTile->image : QImage();

    if (!cachedTile) {
        QImage renderedImage;
        QBrush waveBrush = m_waveBrush;;
        QColor outlineColor = m_clip->is_muted()
            ? themer()->get_color("AudioClip:wavemacroview:outline:muted")
            : themer()->get_color("AudioClip:wavemacroview:outline");
        QColor separatorColor = m_clip->is_selected()
            ? themer()->get_color("AudioClip:channelseperator:selected")
            : themer()->get_color("AudioClip:channelseperator");
        QColor microWaveColor = themer()->get_color("AudioClip:wavemicroview");
        QColor noSignalColor = minINFLineColor;

        renderedImage = QImage(QSize(qRound(width * devicePixelRatio), qRound(m_height * devicePixelRatio)),
                       QImage::Format_ARGB32_Premultiplied);
        renderedImage.setDevicePixelRatio(devicePixelRatio);
        renderedImage.fill(Qt::transparent);
        QPainter tilePainter(&renderedImage);
        tilePainter.setRenderHint(QPainter::Antialiasing, false);

        const int channelHeight = m_height / int(m_mergedView ? 1 : channels);
        // The tile cache is aligned to the source-space pixel grid. The tile is then
        // drawn back to its clip-local x-position, so left-edge changes in the clip source
        // offset do not require tile invalidation.
        const int dataOffset = qMax(0, qCeil(-(renderStart + sourceStartPixels)));
        const int sampleWidth = qMax(0, width - dataOffset);
        const int peakDataCount = microView ? sampleWidth : sampleWidth * 2;
        
        // curveMixdown stores the gain curve to apply to this tile -- starts at all 1.0 (no gain applied)
        QVarLengthArray<float> curveMixdown(width, 1.0f);
        Curve* clipCurve = m_clip->get_plugin_chain()->get_fader()->get_curve();
        bool hasCurve = false;
        const double clipCurveOffset = sourceStartPixels;
        const double trackCurveOffset = double(m_clip->get_track_start_location() / timeRefScale);

        auto trackGainCurve = m_tv->get_gain_curve_view()->get_curve();
        auto fadeIn = m_clip->get_fade_in();
        auto fadeOut = m_clip->get_fade_out();

        if (clipCurve && (!clipCurve->is_trivial() || !qFuzzyCompare(clipCurve->get_trivial_gain(), 1.0f))) {
            // apply the clip's gain curve to curveMixdown, if it exists
            hasCurve = true;
            // this is the first gain curve, so just replace the original 1.0 values
            if (clipCurve->is_trivial()) {
                curveMixdown = QVarLengthArray<float>(width, clipCurve->get_trivial_gain());
            }
            else {
                clipCurve->get_vector((renderStart + clipCurveOffset) * m_sv->timeref_scalefactor,
                                        (renderStart + clipCurveOffset + width) * m_sv->timeref_scalefactor,
                                        curveMixdown.data(), width);
            }
        }
        if (trackGainCurve && (!trackGainCurve->is_trivial() || !qFuzzyCompare(trackGainCurve->get_trivial_gain(), 1.0f))) {
            // apply the cltrack's gain curve to curveMixdown, if it exists
            hasCurve = true;
            QVarLengthArray<float> trackMixdown(width);
            const qreal trackRenderStart = renderStart + trackCurveOffset - tilePadding;
            if (trackGainCurve->is_trivial()) {
                trackMixdown = QVarLengthArray<float>(width, trackGainCurve->get_trivial_gain());
            }
            else {
                trackGainCurve->get_vector(trackRenderStart * m_sv->timeref_scalefactor,
                                            (trackRenderStart + width) * m_sv->timeref_scalefactor,
                                            trackMixdown.data(), width);
            }
            // this is not necessarily the first gain curve, so multiply values into the existing ones
            // This should get optimized by the compiler into fancy vector operations
            for (int index = 0; index < width; ++index) {
                curveMixdown[index] *= trackMixdown[index];
            }
        }
        
        // Now multiply in any relevant fade in and fade out curves that overlap with this tile
        const qreal clipWidth = double(m_clip->get_length() / timeRefScale);
        for (FadeCurve* fade : {fadeIn, fadeOut}) {
            if (!fade) continue;
            const int fadeWidth = double(fade->get_range() / m_sv->timeref_scalefactor);
            if (fadeWidth == 0) continue;
            hasCurve = true;
            const qreal fadeStart = fade->get_fade_type() == FadeCurve::FadeOut ? clipWidth - fadeWidth : 0.0;
            const int first = qMax(0, qCeil(fadeStart - renderStart));
            const int last = qMin(width, qFloor(fadeStart + fadeWidth - renderStart) + 1);
            if (last <= first || fadeWidth <= 0.0) continue;
            QVarLengthArray<float> fadeValues(last - first);
            const qreal vectorStart = (renderStart + first - fadeStart) * m_sv->timeref_scalefactor;
            fade->get_vector(vectorStart, vectorStart + (last - first) * m_sv->timeref_scalefactor,
                             fadeValues.data(), last - first);
            for (int index = first; index < last; ++index) {
                curveMixdown[index] *= fadeValues[index - first];
            }
        }

        // Load peak data, mix curvedata and start painting it
        // if no peakdata is returned for a certain Peak object, schedule it for loading.
        Peak* peak = m_clip->get_peak();

        for (uint channel = 0; channel < channels; ++channel) {
            if (m_mergedView && channels == 2 && channel == 0) continue;
            float* pixelData = nullptr;
            const int availpeaks = peak->calculate_peaks(
                int(channel), &pixelData,
                TimeRef((renderStart + dataOffset) * m_sv->timeref_scalefactor) + m_clip->get_source_start_location(),
                peakDataCount, zoom);

            if (availpeaks == Peak::NO_PEAK_FILE && !m_waitingForPeaks) {
                connect(peak, SIGNAL(progress(int)), this, SLOT(update_progress_info(int)));
                connect(peak, SIGNAL(finished()), this, SLOT (peak_creation_finished()));
                m_waitingForPeaks = true;
                peak->start_peak_loading();
                return;
            }

            if (availpeaks == Peak::PERMANENT_FAILURE || availpeaks == Peak::NO_PEAKDATA_FOUND) {
                return;
            }

            const int height = m_mergedView ? m_height : channelHeight;
            const int center = m_mergedView ? height / 2 : channelHeight / 2 + int(channel) * channelHeight;
            const float scale = microView
                ? float(height) * 0.90f / 2.0f * m_clip->get_gain()
                : float(height) * (m_classicView ? 0.90f / (Peak::MAX_DB_VALUE * 2.0f) : 0.95f / Peak::MAX_DB_VALUE) * m_clip->get_gain();
            QPolygonF polygon;
            polygon.reserve(microView ? width : width * 2 + 2);

            if (microView) {
                // Microview, paint waveform as polyline
                for (int x = dataOffset; x < width && x - dataOffset < availpeaks; ++x) {
                    polygon.append(QPointF(x, center - scale * curveMixdown[x] * pixelData[x - dataOffset]));
                }
                tilePainter.setPen(noSignalColor);
                tilePainter.drawLine(0, center, width, center);
                tilePainter.setPen(microWaveColor);
                tilePainter.drawPolyline(polygon);
            }
            else {
                // Not microview, paint as a filled-in polygon
                tilePainter.setBrush(m_fillwave ? waveBrush : Qt::NoBrush);
                if (m_paintWithOutline) {
                    if (m_clip->is_muted()) {
                        tilePainter.setPen(themer()->get_color("AudioClip:wavemacroview:outline:muted"));
                    } else {
                        tilePainter.setPen(themer()->get_color("AudioClip:wavemacroview:outline"));
                    }
                } else {
                    tilePainter.setPen(Qt::NoPen);
                }
                if (m_classicView) {
                    // ClassicView uses both positive and negative values,
                    // rectified view: pick the highest value of both
                    // Merged view: calculate highest value for all channels,
                    // and store it in the first channels pixeldata.
                    for (int x = dataOffset; x < width && (x - dataOffset) * 2 + 1 < availpeaks; ++x) {
                        polygon.append(QPointF(x, center - scale * curveMixdown[x] * pixelData[(x - dataOffset) * 2]));
                    }
                    for (int x = polygon.size() - 1; x >= 0; --x) {
                        const int sample = (m_mergedView) ? int(polygon.at(x).x()) : x;
                        const int pixel = sample - dataOffset;
                        polygon.append(QPointF(sample, center + scale * curveMixdown[sample] * pixelData[pixel * 2 + 1]));
                    }
                    tilePainter.drawPolygon(polygon);
                } else {
                    // if Rectified View, calculate max of the minimum and maximum value.
                    QVarLengthArray<float> rectified(width);
                    const int rectifiedCount = qMin(width, availpeaks / 2);
                    const int base = m_mergedView ? height : int(channel+1) * channelHeight;
                    for (int x = 0; x < rectifiedCount; ++x) {
                        const int pixel = x - dataOffset;
                        rectified[x] = -std::fabs(f_max(pixelData[pixel * 2], -pixelData[pixel * 2 + 1]));
                        const float curveValue = hasCurve ? curveMixdown[x] : 1.0f;
                        polygon.append(QPointF(x, base + scale * curveValue * rectified[x]));
                    }
                    polygon.append(QPointF(width, base));
                    polygon.append(QPointF(0, base));
                    tilePainter.drawPolygon(polygon);
                }
            }
        }
        tilePainter.end();

        // We overpaint by a few pixels on each end to avoid weird artifacts.  Clip those edges back off.
        cachedImage = renderedImage.copy(qRound(tilePadding * devicePixelRatio), 0,
                        qRound(ClipTileWidth * devicePixelRatio), qRound(m_height * devicePixelRatio));

        if (!microView && !m_waitingForPeaks) {
            // Save the image into the cache, as long as it's not a microView tile
            // (microView tiles are too many to cache, and faster to paint, so it matters less.)
            ClipTile& tile = ctcache().insert(m_clip, hash, tileX, 
                QSize(qRound(ClipTileWidth * devicePixelRatio), qRound(m_height * devicePixelRatio)));
            tile.image.setDevicePixelRatio(devicePixelRatio);
            tile.image = cachedImage;
        }
    }

    painter->drawImage(QPointF(tileX - sourceStartPixels, 0), cachedImage);
}

void AudioClipView::draw_clipinfo_area(QPainter* p, double xstart)
{
    if (xstart > m_clipInfo.width()) {
        return;
    }

    int margin = 6;
    p->drawPixmap(margin, m_height - m_clipInfo.height() - margin, m_clipInfo);
}


void AudioClipView::draw_db_lines(QPainter* p, qreal xstart, int pixelcount)
{
    p->save();

    int channels = m_clip->get_channel_count();
    bool microView = m_sheet->get_hzoom() < 64 ? true : false;
    int linestartpos = xstart;
    if (xstart < m_lineOffset) linestartpos = m_lineOffset;

    if ((m_mergedView) || (channels == 0)) {
        channels = 1;
    }

    // calculate the height of one channel
    int height = m_height / channels;

    p->setPen(themer()->get_color("AudioClip:db-grid"));
    p->setFont( themer()->get_font("AudioClip:fontscale:dblines") );

    if (m_classicView || microView) { // classicView = non-rectified

        // translate the painter to set the first channel center line to 0
        p->translate(0, height / 2.0);

        // determine the distance of the db line from the center line
        int zeroDb = 0.9 * height / 2;
        int msixDb = 0.9 * height / 4;

        // draw the lines above and below the center line, then translate
        // the painter to the next channel
        for (int i = 0; i < channels; ++i) {
            p->drawLine(linestartpos, zeroDb, xstart+pixelcount, zeroDb);
            p->drawLine(linestartpos, -zeroDb, xstart+pixelcount, -zeroDb);
            p->drawLine(linestartpos, msixDb, xstart+pixelcount, msixDb);
            p->drawLine(linestartpos, -msixDb + 1, xstart+pixelcount, -msixDb + 1);

            if (xstart < m_lineOffset) {
                p->drawText(0.0, zeroDb - 1 + m_lineVOffset, "  0 dB");
                p->drawText(0.0, -zeroDb + m_lineVOffset, "  0 dB");
                p->drawText(0.0, msixDb + m_lineVOffset, " -6 dB");
                p->drawText(0.0, -msixDb + m_lineVOffset, " -6 dB");
            }


            p->translate(0, height);
        }
    } else {  // rectified

        // translate the painter to set the first channel base line to 0
        p->translate(0, height);

        // determine the distance of the db line from the center line
        int zeroDb = 0.95 * height;
        int msixDb = 0.95 * height / 2;

        // draw the lines above the center line, then translate
        // the painter to the next channel
        for (int i = 0; i < channels; ++i) {
            p->drawLine(linestartpos, -zeroDb, xstart+pixelcount, -zeroDb);
            p->drawLine(linestartpos, -msixDb + 1, xstart+pixelcount, -msixDb + 1);

            if (xstart < m_lineOffset) {
                p->drawText(0.0, -zeroDb + m_lineVOffset, "  0 dB");
                p->drawText(0.0, -msixDb + m_lineVOffset, " -6 dB");
            }

            p->translate(0, height);
        }
    }

    p->restore();
}

void AudioClipView::create_brushes()
{
    /** TODO: The following part is identical to calculations in draw_db_lines(). Move to a central place. **/
    bool microView = m_sheet->get_hzoom() < 64 ? true : false;
    int channels = m_clip->get_channel_count();

    if ((m_mergedView) || (channels == 0)) {
        channels = 1;
    }

    // calculate the height of one channel
    int height = m_height / channels;

    if (m_classicView || microView)
    {
        height *= 0.45;
    } else {
        height *= 0.95;
    }
    /** end of TODO **/

    // create brushes for background states
    m_brushBgRecording = themer()->get_brush("AudioClip:background:recording", QPoint(0, 0), QPoint(0, -m_height));
    m_brushBgMuted = themer()->get_brush("AudioClip:background:muted", QPoint(0, 0), QPoint(0, -m_height));
    m_brushBgMutedHover = themer()->get_brush("AudioClip:background:muted:mousehover", QPoint(0, 0), QPoint(0, -m_height));
    m_brushBgSelected  = themer()->get_brush("AudioClip:background:selected", QPoint(0, 0), QPoint(0, -m_height));
    m_brushBgSelectedHover = themer()->get_brush("AudioClip:background:selected:mousehover", QPoint(0, 0), QPoint(0, -m_height));
    m_brushBg = themer()->get_brush("AudioClip:background", QPoint(0, 0), QPoint(0, -m_height));
    m_brushBgHover = themer()->get_brush("AudioClip:background:mousehover", QPoint(0, 0), QPoint(0, -m_height));

    // brushes for the wave form
    m_brushFg = themer()->get_brush("AudioClip:wavemacroview:brush", QPoint(0, 0), QPoint(0, -height));
    m_brushFgHover = themer()->get_brush("AudioClip:wavemacroview:brush:hover", QPoint(0, 0), QPoint(0, -height));
    m_brushFgMuted = themer()->get_brush("AudioClip:wavemacroview:brush:muted", QPoint(0, 0), QPoint(0, -height));
    m_brushFgEdit = themer()->get_brush("AudioClip:wavemacroview:brush:curvemode", QPoint(0, 0), QPoint(0, -height));
    m_brushFgEditHover = themer()->get_brush("AudioClip:wavemacroview:brush:curvemode:hover", QPoint(0, 0), QPoint(0, -height));
}

void AudioClipView::create_clipinfo_string()
{
    PENTER;
    QFont font = themer()->get_font("AudioClip:fontscale:title");
    QFontMetrics fm(font);

    QString clipinfoString = fm.elidedText(m_clip->get_name(), Qt::ElideRight, 200);

    int clipInfoWidth = fm.boundingRect(clipinfoString).width();

    m_clipInfo = QPixmap(clipInfoWidth, m_infoAreaHeight);
    m_clipInfo.fill(Qt::transparent);
    QColor textColor = themer()->get_color("AudioClip:text");

    QPainter painter(&m_clipInfo);
    painter.setFont(font);
    painter.setPen(textColor);
    painter.drawText(m_clipInfo.rect(), clipinfoString);
}

void AudioClipView::update_progress_info( int progress )
{
    m_progress = progress;
    update(10, 0, 150, m_height);
}

void AudioClipView::peak_creation_finished()
{
    m_waitingForPeaks = false;
    update();
}

void AudioClipView::add_new_fade_curve_view( FadeCurve * fade )
{
    PENTER;
    FadeCurveView* view = new FadeCurveView(m_sv, this, fade);
    m_FadeCurveViews.append(view);
    connect(view, SIGNAL(fadeModified()), m_sv, SLOT(stop_follow_play_head()));
    connect(fade, SIGNAL(rangeChanged()), this, SLOT(invalidate_fade_tiles()));
    connect(fade, SIGNAL(stateChanged()), this, SLOT(invalidate_fade_tiles()));
    connect(fade, SIGNAL(bendValueChanged()), this, SLOT(invalidate_fade_tiles()));
    connect(fade, SIGNAL(strengthValueChanged()), this, SLOT(invalidate_fade_tiles()));
    connect(fade, SIGNAL(modeChanged()), this, SLOT(invalidate_fade_tiles()));
    connect(fade, SIGNAL(rasterChanged()), this, SLOT(invalidate_fade_tiles()));
}

void AudioClipView::remove_fade_curve_view( FadeCurve * fade )
{
    for (int i = 0; i < m_FadeCurveViews.size(); ++i) {
        FadeCurveView* view = m_FadeCurveViews.at(i);
        if (view->get_fade() == fade) {
            m_FadeCurveViews.takeAt(i);
            scene()->removeItem(view);
            delete view;
            break;
        }
    }
}

void AudioClipView::calculate_bounding_rect()
{
    PENTER4;
    prepareGeometryChange();

    m_height = m_parentViewItem->get_height();
    m_boundingRect = QRectF(0, 0, (double(m_clip->get_length().universal_frame()) / m_sv->timeref_scalefactor), m_height);

    uint channelCount = m_clip->get_channel_count();

    // A silent clip readsource on purpose has 0 channels
    // catch and deal with this to avoid a division by 0 below
    if  (channelCount == 0) {
        channelCount = 1;
    }

    if ((m_height / int(channelCount)) < 30) {
        m_classicView = false;
    } else {
        m_classicView = ! config().get_property("Themer", "paintaudiorectified", false).toBool();
    }

    if (m_gainCurveView) {
        m_gainCurveView->updateNodeVisibility(0, (m_clip->get_track_end_location()-m_clip->get_track_start_location()) / m_sv->timeref_scalefactor);
    }
    update_start_pos();
    ViewItem::calculate_bounding_rect();
}


void AudioClipView::repaint( )
{
    update(m_boundingRect);
}

void AudioClipView::invalidate_clip_tiles()
{
    ctcache().invalidate_clip(m_clip);
    update(m_boundingRect);
}

void AudioClipView::invalidate_fade_tiles()
{
    auto* fade = qobject_cast<FadeCurve*>(sender());
    for (FadeCurveView *fadeView : m_FadeCurveViews) {
        if (fadeView->get_fade() == fade) {
            const qreal sourceStartPixels = double(m_clip->get_source_start_location() / m_sv->timeref_scalefactor);
            const qreal left = fadeView->pos().x() + sourceStartPixels;
            const qreal right = left + fadeView->boundingRect().width();
            ctcache().invalidate_clip_range(m_clip, m_sheet->get_hzoom(), left, right);
            update(QRectF(fadeView->pos().x() - 4, 0, fadeView->boundingRect().width() + 8, m_height));
            return;
        }
    }
}

void AudioClipView::invalidate_edge_tiles(bool isLeftEdge)
{
    for (FadeCurveView *fadeView : m_FadeCurveViews) {
        if ((isLeftEdge && fadeView->get_fade()->get_fade_type() == FadeCurve::FadeIn) ||
            (!isLeftEdge && fadeView->get_fade()->get_fade_type() == FadeCurve::FadeOut))
        {
            int range = fadeView->get_fade()->get_range() / m_sv->timeref_scalefactor;
            if (range > 0) {
                const qreal sourceStartPixels = double(m_clip->get_source_start_location() / m_sv->timeref_scalefactor);
                const qreal left = fadeView->pos().x() + sourceStartPixels;
                const qreal right = left + fadeView->boundingRect().width();
                ctcache().invalidate_clip_range(m_clip, m_sheet->get_hzoom(), left, right);
                update(QRectF(fadeView->pos().x() - 4, 0, fadeView->boundingRect().width() + 8, m_height));
            }
            return;
        }
    }
}

void AudioClipView::invalidate_tiles_range(int startx, int endx)
{
    const qreal sourceStartPixels = double(m_clip->get_source_start_location() / m_sv->timeref_scalefactor);
    const qreal left = startx - (m_clip->get_track_start_location() / m_sv->timeref_scalefactor) + sourceStartPixels;
    const qreal right = endx - (m_clip->get_track_start_location() / m_sv->timeref_scalefactor) + sourceStartPixels;
    ctcache().invalidate_clip_range(m_clip, m_sheet->get_hzoom(), left, right);
    update(QRectF(startx-2, 0, endx-startx+4, m_height));
}

void AudioClipView::update_start_pos()
{
    // 	printf("AudioClipView::update_start_pos()\n");
    setPos((double(m_clip->get_track_start_location().universal_frame()) / m_sv->timeref_scalefactor), 0);
}

TCommand * AudioClipView::fade_range()
{
    Q_ASSERT(m_sheet);
    qreal x = cpointer().on_first_input_event_scene_x() - scenePos().x();

    if (x < (m_boundingRect.width() / 2)) {
        return clip_fade_in();
    } 
    return clip_fade_out();
}

TCommand * AudioClipView::clip_fade_in( )
{
    if (! m_clip->get_fade_in()) {
        // This implicitely creates the fadecurve
        m_clip->set_fade_in(1);
    }
    return new FadeRange(m_clip, m_clip->get_fade_in(), m_sv->timeref_scalefactor);
}

TCommand * AudioClipView::clip_fade_out( )
{
    if (! m_clip->get_fade_out()) {
        m_clip->set_fade_out(1);
    }
    return new FadeRange(m_clip, m_clip->get_fade_out(), m_sv->timeref_scalefactor);
}

TCommand * AudioClipView::reset_fade()
{
    Q_ASSERT(m_sheet);
    qreal x = cpointer().on_first_input_event_scene_x() - scenePos().x();

    if (x < (m_boundingRect.width() / 2)) {
        return m_clip->reset_fade_in();
    } 
    return m_clip->reset_fade_out();
}

void AudioClipView::position_changed()
{
    // invalidate_clip_tiles();
    // Update the curveview start offset, only needed for left edge dragging
    // but who cares :)
    // the calculate_bounding_rect() will update AudioClipViews children, so
    // the CurveView and it's nodes get updated as well, no need to set
    // the start offset for those manually!
    m_gainCurveView->set_start_offset(m_clip->get_source_start_location());
    if (!m_tv->get_gain_curve_view()->get_curve()->is_trivial()) {
        // If the track has a non-constant gain curve, we need to invalidate tiles on clip position movement
        this->invalidate_clip_tiles();
    }
    calculate_bounding_rect();
}

void AudioClipView::load_theme_data()
{
    m_drawbackground = themer()->get_property("AudioClip:drawbackground", 1).toInt();
    m_infoAreaHeight = themer()->get_property("AudioClip:infoareaheight", 16).toInt();
    m_mimimumheightforinfoarea = themer()->get_property("AudioClip:mimimumheightforinfoarea", 45).toInt();
    m_classicView = ! config().get_property("Themer", "paintaudiorectified", false).toBool();
    m_mergedView = config().get_property("Themer", "paintstereoaudioasmono", false).toBool();
    m_fillwave = themer()->get_property("AudioClip:fillwave", 1).toInt();
    minINFLineColor = themer()->get_color("AudioClip:channelseperator");
    m_paintWithOutline = config().get_property("Themer", "paintwavewithoutline", true).toBool();
    m_drawDbGrid = config().get_property("Themer", "drawdbgrid", false).toBool();
    calculate_bounding_rect();

    QFont dblfont = themer()->get_font("AudioClip:fontscale:dblines");
    QFontMetrics fm(dblfont);
    m_lineOffset = fm.horizontalAdvance(" -6 dB ");
    m_lineVOffset = fm.ascent()/2;

    create_brushes();
    create_clipinfo_string();
}


void AudioClipView::active_context_changed()
{
    if (ied().is_holding()) {
        // TODO: find out if we still need to bail out
        // when holding is active, say for moving a clip with [ D ]
        // or something else?
        //                return;
    }

    if (m_clip->has_active_context()) {
        m_tv->to_front(this);
    }

    update(m_boundingRect);
}


TCommand * AudioClipView::select_fade_in_shape( )
{
    TMainWindow::instance()->select_fade_in_shape();

    return nullptr;
}

TCommand * AudioClipView::select_fade_out_shape( )
{
    TMainWindow::instance()->select_fade_out_shape();

    return nullptr;
}

void AudioClipView::start_recording()
{
    m_oldRecordingPos = TimeRef();
    connect(&m_recordingTimer, SIGNAL(timeout()), this, SLOT(update_recording()));
    m_recordingTimer.start(750);
}

void AudioClipView::finish_recording()
{
    m_recordingTimer.stop();
    prepareGeometryChange();
    m_boundingRect = QRectF(0, 0, (m_clip->get_length() / m_sv->timeref_scalefactor), m_height);
    m_gainCurveView->calculate_bounding_rect();
    update();
}

void AudioClipView::update_recording()
{
    if (m_clip->recording_state() != AudioClip::RECORDING) {
        return;
    }

    TimeRef newPos = m_clip->get_length();
    m_boundingRect = QRectF(0, 0, (newPos / m_sv->timeref_scalefactor), m_height);

    int updatewidth = int((newPos - m_oldRecordingPos) / m_sv->timeref_scalefactor);
    QRect updaterect = QRect(int(m_oldRecordingPos / m_sv->timeref_scalefactor) - 1, 0, updatewidth + 1, m_height);
    update(updaterect);
    m_oldRecordingPos = newPos;
}

TCommand * AudioClipView::set_audio_file()
{
    if (m_clip->is_readsource_invalid()) {
        ReadSource* rs = m_clip->get_readsource();
        if ( ! rs ) {
            return ied().failure();
        }

        QString filename = QFileDialog::getOpenFileName(TMainWindow::instance(),
                                                        tr("Reset Audio File for Clip: %1").arg(m_clip->get_name()),
                                                        rs->get_filename(),
                                                        tr("All files (*);;Audio files (*.wav *.flac *.ogg *.mp3 *.m4a *.mp4 *.aac *.wv *.w64)"));

        if (filename.isEmpty()) {
            info().information(tr("No file selected!"));
            return ied().failure();
        }

        if (rs->set_file(filename) < 0) {
            return ied().failure();
        }

        resources_manager()->set_source_for_clip(m_clip, rs);


        // FIXME This is a hack. When a ReadSource didn't have a valid file it wasn't added
        // to DiskIO in AudioClip::set_sheet(). So when resetting the audiofile this solves it,
        // but it's not the proper place to do so!!
        m_clip->set_sheet(m_sheet);

        info().information(tr("Succesfully set AudioClip file to %1").arg(filename));

        return ied().succes();
    }

    return ied().did_not_implement();
}

TCommand * AudioClipView::edit_properties()
{
    auto editdialog = new AudioClipEditDialog(m_clip, TMainWindow::instance());

    editdialog->show();

    return nullptr;
}

void AudioClipView::clip_state_changed()
{
    invalidate_clip_tiles();
    update();
}
