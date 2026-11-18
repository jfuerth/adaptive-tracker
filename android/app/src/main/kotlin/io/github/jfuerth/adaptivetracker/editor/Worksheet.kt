package io.github.jfuerth.adaptivetracker.editor

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.graphics.drawscope.withTransform
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.TextLayoutResult
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.jfuerth.adaptivetracker.NativeAudio
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min

/** The worksheet's cursor: a field of a cell, in one time round ([instance]: its start tick) of a pattern. */
data class SheetCursor(val lane: Int, val instance: Long, val pattern: Int, val row: Int, val column: Int, val field: Field)

/** Engine ticks per beat; NativeAudio.getPlayheads counts these. */
private const val ENGINE_TICKS_PER_BEAT = 96

private val CELL_STYLE = TextStyle(fontFamily = FontFamily.Monospace, fontSize = 15.sp)
private val CELL_BOLD = CELL_STYLE.copy(fontWeight = FontWeight.Bold)
private val LABEL_STYLE = TextStyle(fontSize = 11.sp)
private val HEADER_STYLE = TextStyle(fontSize = 13.sp)

/** Smallest and largest zoom: far enough out to see many bars, in to read comfortably. */
private const val MIN_ZOOM = 0.12f
private const val MAX_ZOOM = 3f

/**
 * The arrangement worksheet: a section's channels side by side, time running
 * down, each channel's patterns repeating forever. Drag to pan, pinch to
 * zoom, tap a cell to put the cursor there ([onTap]). The bar numbers stay
 * on the left and the channel names on top. Zoomed out, notes become marks.
 */
@Composable
fun Worksheet(
    arrangement: Arrangement,
    ctx: PitchContext,
    channelNames: List<String>,
    cursor: SheetCursor?,
    playing: Boolean,
    follow: Boolean,
    onUserMove: () -> Unit,
    onTap: (SheetCursor) -> Unit,
    modifier: Modifier = Modifier,
) {
    val colors = MaterialTheme.colorScheme
    val density = LocalDensity.current
    val measurer = rememberTextMeasurer()
    val texts = remember { TextCache(measurer) }

    // Layout in world pixels (zoom 1): a speed-6 row is as tall as in the pattern editor.
    val geometry = remember(arrangement, density) { Geometry(arrangement, measurer, density.density) }
    val g = geometry

    var panX by remember { mutableFloatStateOf(0f) }
    var panY by remember { mutableFloatStateOf(0f) }
    var zoom by remember { mutableFloatStateOf(1f) }
    var viewport by remember { mutableStateOf(IntSize.Zero) }
    // Each lane's playhead (null: not playing this lane's sequence). Read only
    // while drawing, so a new position redraws without recomposing.
    var heads by remember { mutableStateOf<List<Arrangement.Playhead?>>(emptyList()) }
    // The visible part of the sheet, in world pixels.
    fun viewW() = (viewport.width - g.gutter) / zoom
    fun viewH() = (viewport.height - g.header) / zoom
    fun clamp() {
        zoom = zoom.coerceIn(MIN_ZOOM, MAX_ZOOM)
        panX = panX.coerceIn(-viewW() * 0.5f, max(-viewW() * 0.5f, g.width - viewW() * 0.5f))
        panY = panY.coerceAtLeast(-viewH() * 0.3f)
    }

    // Every frame, before drawing: where the engine says each channel is,
    // placed in the layout, and (following) the latest playhead a third of the
    // way down. Panning here, in the same frame, keeps the scroll even.
    val playingNow by rememberUpdatedState(playing)
    val followNow by rememberUpdatedState(follow)
    LaunchedEffect(arrangement, geometry) {
        while (true) {
            withFrameNanos {
                val now = if (playingNow) playheads(arrangement, NativeAudio.getPlayheads()) else emptyList()
                if (now != heads) heads = now
                val latest = now.maxOfOrNull { it?.tick ?: -1f } ?: -1f
                if (followNow && latest >= 0 && viewport.height > 0) {
                    panY = latest * g.pxPerTick - viewH() / 3f
                    clamp()
                }
            }
        }
    }
    // The cursor stays in view when it moves (typing advances it).
    LaunchedEffect(cursor, viewport) {
        val c = cursor ?: return@LaunchedEffect
        if (viewport.height == 0) return@LaunchedEffect
        val lane = arrangement.lanes.getOrNull(c.lane) ?: return@LaunchedEffect
        val placed = lane.firstOf(c.pattern, c.instance) ?: return@LaunchedEffect
        val top = placed.rowStart(c.row) * g.pxPerTick
        val bottom = top + placed.entry.ticksPerRow * g.pxPerTick
        if (top < panY || bottom > panY + viewH()) {
            panY = top - viewH() * 0.4f
            clamp()
        }
        val left = g.cellX(c.lane, c.column)
        if (left < panX || left + g.colW > panX + viewW()) {
            panX = left - g.labelStrip
            clamp()
        }
    }

    val tapped by rememberUpdatedState(onTap)
    val moved by rememberUpdatedState(onUserMove)
    Canvas(
        modifier
            .onSizeChanged { viewport = it }
            .pointerInput(geometry) {
                detectTransformGestures { centroid, pan, gestureZoom, _ ->
                    moved()
                    // Zoom about the fingers: the world point under them stays put.
                    val cx = (centroid.x - g.gutter) / zoom + panX
                    val cy = (centroid.y - g.header) / zoom + panY
                    zoom = (zoom * gestureZoom).coerceIn(MIN_ZOOM, MAX_ZOOM)
                    panX = cx - (centroid.x - g.gutter) / zoom - pan.x / zoom
                    panY = cy - (centroid.y - g.header) / zoom - pan.y / zoom
                    clamp()
                }
            }
            .pointerInput(geometry) {
                detectTapGestures { pos ->
                    if (pos.x < g.gutter || pos.y < g.header) return@detectTapGestures
                    val wx = (pos.x - g.gutter) / zoom + panX
                    val wy = (pos.y - g.header) / zoom + panY
                    g.hit(wx, wy)?.let { tapped(it) }
                }
            },
    ) {
        if (viewport.width == 0) return@Canvas
        val t0 = (panY / g.pxPerTick).toLong() - 1
        val t1 = ((panY + viewH()) / g.pxPerTick).toLong() + 1

        // ---- The sheet, in world coordinates ----
        clipRect(g.gutter, g.header, this.size.width, this.size.height) {
            withTransform({
                translate(g.gutter - panX * zoom, g.header - panY * zoom)
                scale(zoom, zoom, pivot = Offset.Zero)
            }) {
                drawBeats(g, arrangement, t0, t1, zoom, colors.outlineVariant, colors.outline)
                arrangement.lanes.forEachIndexed { laneIndex, lane ->
                    val head = heads.getOrNull(laneIndex)
                    for (placed in lane.between(t0, t1)) {
                        drawInstance(
                            g, texts, ctx, laneIndex, placed, t0, t1, zoom, panY,
                            playRow = if (head?.placed == placed) head.row else -1,
                            cursor?.takeIf { it.lane == laneIndex },
                            sameAsCursor = cursor?.pattern == placed.entry.pattern,
                            colors = SheetColors(
                                laneTint = colors.surfaceVariant,
                                sameTint = colors.secondaryContainer,
                                edge = colors.outline,
                                text = colors.onSurface,
                                dim = colors.onSurfaceVariant.copy(alpha = 0.55f),
                                note = colors.primary,
                                cursorRow = colors.primaryContainer,
                                cursorField = colors.secondary.copy(alpha = 0.45f),
                                label = colors.onSurfaceVariant,
                            ),
                        )
                    }
                    if (head != null) {
                        val y = head.tick * g.pxPerTick
                        drawLine(colors.tertiary, Offset(g.laneX[laneIndex], y), Offset(g.laneX[laneIndex] + g.laneW[laneIndex], y), strokeWidth = 2f / zoom)
                    }
                }
            }
        }

        // ---- Bar numbers, fixed on the left ----
        drawRect(colors.surface, Offset(0f, g.header), Size(g.gutter, this.size.height - g.header))
        val barPx = arrangement.barTicks * g.pxPerTick * zoom
        if (barPx > 0) {
            val every = max(1, ceil(18.dp.toPx() / barPx).toInt())
            var bar = max(0L, floor(panY / (arrangement.barTicks * g.pxPerTick)).toLong())
            while (true) {
                val y = g.header + (bar * arrangement.barTicks * g.pxPerTick - panY) * zoom
                if (y > this.size.height) break
                if (bar % every == 0L && y >= g.header - 2f) {
                    val label = texts.layout("${bar + 1}", LABEL_STYLE)
                    drawText(label, colors.primary, Offset(g.gutter - label.size.width - 6f, y + 2f))
                }
                bar++
            }
        }

        // ---- Channel names, fixed on top ----
        drawRect(colors.surface, Offset.Zero, Size(this.size.width, g.header))
        clipRect(g.gutter, 0f, this.size.width, g.header) {
            arrangement.lanes.forEachIndexed { i, lane ->
                val left = g.gutter + (g.laneX[i] - panX) * zoom
                val width = g.laneW[i] * zoom
                if (left + width < g.gutter || left > this.size.width) return@forEachIndexed
                val name = channelNames.getOrElse(lane.channel) { "Channel ${lane.channel + 1}" }
                val layout = texts.header(name, width.toInt().coerceAtLeast(1))
                drawText(layout, colors.onSurface, Offset(max(left, g.gutter) + 4f, (g.header - layout.size.height) / 2f))
                drawLine(colors.outlineVariant, Offset(left, g.header - 1f), Offset(left + width, g.header - 1f))
            }
        }
    }
}

private data class SheetColors(
    val laneTint: Color, val sameTint: Color, val edge: Color, val text: Color, val dim: Color, val note: Color,
    val cursorRow: Color, val cursorField: Color, val label: Color,
)

/** Where everything goes, in world pixels. */
private class Geometry(val arrangement: Arrangement, measurer: TextMeasurer, density: Float) {
    val gutter = 32f * density
    val header = 28f * density
    /** Each lane starts with a strip for the pattern names, written along it. */
    val labelStrip = 16f * density
    private val laneGap = 10f * density
    private val colGap = 6f * density
    val charW: Float
    val colW: Float
    val textH: Float
    init {
        val m = measurer.measure("C-4 .. ...", CELL_STYLE)
        charW = m.size.width / 10f
        colW = m.size.width + 4f * density
        textH = m.size.height.toFloat()
    }
    /**
     * A speed-6 row is as tall as in the pattern editor, unless the section has
     * shorter rows: then the shortest row just fits the text, so every row
     * keeps the same text size and longer rows are taller.
     */
    val pxPerTick: Float = run {
        val shortest = arrangement.lanes.flatMap { it.entries }.minOfOrNull { it.ticksPerRow }?.coerceAtLeast(1) ?: 6
        max(26f * density / 6f, (textH + 2f * density) / shortest)
    }
    /** Text sits this far below the top of its row. */
    val textTop = 1f * density
    val laneW = arrangement.lanes.map { labelStrip + it.columns * colW + (it.columns - 1) * colGap }
    val laneX = laneW.runningFold(0f) { x, w -> x + w + laneGap }
    val width = laneX.last()

    fun cellX(lane: Int, column: Int) = laneX[lane] + labelStrip + column * (colW + colGap)

    /** The cell field at world point ([wx], [wy]), or null. */
    fun hit(wx: Float, wy: Float): SheetCursor? {
        val laneIndex = laneW.indices.firstOrNull { wx >= laneX[it] && wx < laneX[it] + laneW[it] } ?: return null
        val lane = arrangement.lanes[laneIndex]
        val placed = lane.at((wy / pxPerTick).toLong()) ?: return null
        val inLane = wx - laneX[laneIndex] - labelStrip
        val column = (inLane / (colW + colGap)).toInt().coerceIn(0, lane.columns - 1)
        val char = ((inLane - column * (colW + colGap)) / charW).toInt()
        val field = when {
            char <= 3 -> Field.NOTE
            char == 4 -> Field.VOLUME_HI
            char <= 6 -> Field.VOLUME_LO
            char == 7 -> Field.EFFECT
            char == 8 -> Field.PARAM_X
            else -> Field.PARAM_Y
        }
        val row = placed.rowAt((wy / pxPerTick).toLong()).coerceAtLeast(0)
        return SheetCursor(laneIndex, placed.start, placed.entry.pattern, row, column, field)
    }
}

/** Text layouts, measured once: cells repeat a lot, and measuring is the slow part. */
private class TextCache(private val measurer: TextMeasurer) {
    private val cells = HashMap<String, TextLayoutResult>()
    private val headers = HashMap<Pair<String, Int>, TextLayoutResult>()

    fun layout(text: String, style: TextStyle = CELL_STYLE): TextLayoutResult {
        if (cells.size > 4000) cells.clear()
        return cells.getOrPut("$text|${style.fontSize}|${style.fontWeight}") { measurer.measure(text, style, softWrap = false, maxLines = 1) }
    }

    /** A channel name, cut to [width] pixels (in steps, so zooming doesn't measure every frame). */
    fun header(text: String, width: Int): TextLayoutResult {
        if (headers.size > 500) headers.clear()
        val w = (width / 16 + 1) * 16
        return headers.getOrPut(text to w) {
            measurer.measure(text, HEADER_STYLE, overflow = TextOverflow.Ellipsis, softWrap = false, maxLines = 1,
                constraints = Constraints(maxWidth = w))
        }
    }
}

/** Beat lines (when far enough apart to see) and stronger bar lines, across every lane. */
private fun DrawScope.drawBeats(g: Geometry, a: Arrangement, t0: Long, t1: Long, zoom: Float, beat: Color, bar: Color) {
    if (a.ticksPerBeat <= 0) return
    val beatPx = a.ticksPerBeat * g.pxPerTick * zoom
    val showBeats = beatPx >= 8f
    var b = max(0L, t0 / a.ticksPerBeat)
    while (b * a.ticksPerBeat <= t1) {
        val isBar = b % a.beatsPerBar == 0L
        if (isBar || showBeats) {
            val y = b * a.ticksPerBeat * g.pxPerTick
            drawLine(if (isBar) bar else beat, Offset(0f, y), Offset(g.width, y), strokeWidth = (if (isBar) 2f else 1f) / zoom)
        }
        b++
    }
}

/** One time round a pattern: background, name, rows (as text, or as marks when too small to read). */
private fun DrawScope.drawInstance(
    g: Geometry,
    texts: TextCache,
    ctx: PitchContext,
    laneIndex: Int,
    placed: Arrangement.Placed,
    t0: Long,
    t1: Long,
    zoom: Float,
    viewTop: Float,
    playRow: Int,
    cursor: SheetCursor?,
    sameAsCursor: Boolean,
    colors: SheetColors,
) {
    val e = placed.entry
    val pattern = PatternStore.get(e.pattern) ?: return
    val info = pattern.info
    val x = g.laneX[laneIndex]
    val w = g.laneW[laneIndex]
    val top = placed.start * g.pxPerTick
    val height = e.length * g.pxPerTick
    val rowH = e.ticksPerRow * g.pxPerTick

    // Background: alternate shades so neighbouring patterns read apart; the
    // pattern under the cursor is tinted everywhere it plays.
    val tint = when {
        sameAsCursor -> colors.sameTint.copy(alpha = 0.5f)
        placed.index % 2 == 0 -> colors.laneTint.copy(alpha = 0.55f)
        else -> colors.laneTint.copy(alpha = 0.25f)
    }
    drawRect(tint, Offset(x, top), Size(w, height))
    drawLine(colors.edge, Offset(x, top), Offset(x + w, top), strokeWidth = 1.5f / zoom)

    // The pattern's name, along the strip at the lane's left.
    val label = texts.layout(PatternStore.names.getOrElse(e.pattern) { "?" }, LABEL_STYLE)
    if (label.size.height * zoom >= 7f) {
        // Pinned to the top of the view while the pattern scrolls past, pushed
        // up by its end.
        val labelTop = min(max(top, viewTop) + 4f / zoom, top + height - label.size.width - 4f / zoom)
        if (labelTop >= top) {
            rotate(90f, pivot = Offset(x, labelTop)) {
                drawText(label, colors.label, Offset(x, labelTop - label.size.height - 1f))
            }
        }
    }

    val first = max(0L, (t0 - placed.start) / e.ticksPerRow).toInt()
    val last = min(e.rows - 1L, (t1 - placed.start) / e.ticksPerRow).toInt()
    if (first > last) return
    // Below a readable size on screen, notes are drawn as marks instead.
    val readable = g.textH * zoom >= 9f

    for (r in first..last) {
        val y = top + r * rowH
        // The playing row is bold: a background would smear against smooth scrolling.
        val bold = r == playRow
        if (cursor != null && cursor.instance == placed.start && cursor.row == r) {
            drawRect(colors.cursorRow, Offset(x + g.labelStrip, y), Size(w - g.labelStrip, rowH))
            val cx = g.cellX(laneIndex, cursor.column)
            val (from, chars) = when (cursor.field) {
                Field.NOTE -> 0 to 3
                Field.VOLUME_HI -> 4 to 1
                Field.VOLUME_LO -> 5 to 1
                Field.EFFECT -> 7 to 1
                Field.PARAM_X -> 8 to 1
                Field.PARAM_Y -> 9 to 1
            }
            drawRect(colors.cursorField, Offset(cx + from * g.charW, y), Size(chars * g.charW, rowH))
        }
        for (c in 0 until info.columns) {
            val cell = pattern.cell(r, c)
            val cx = g.cellX(laneIndex, c)
            if (readable) {
                if (cell.isEmpty && cursor?.row != r) {
                    // Empty cells: a quiet dot pattern keeps the grid readable without the noise.
                    val dots = texts.layout("··· ·· ···")
                    drawText(dots, colors.dim, Offset(cx, y + g.textTop))
                } else {
                    val text = CellFormat.note(cell, info.modeOf(c), ctx) + " " + CellFormat.volume(cell) + " " + CellFormat.effect(cell)
                    drawText(texts.layout(text, if (bold) CELL_BOLD else CELL_STYLE), if (cell.isNote || cell.isOff) colors.text else colors.dim, Offset(cx, y + g.textTop))
                }
            } else if (cell.isNote) {
                val markH = max(rowH * 0.7f, 2f / zoom)
                drawRect(colors.note, Offset(cx, y), Size(g.colW * 0.6f, markH))
            } else if (cell.isOff) {
                drawRect(colors.dim, Offset(cx, y), Size(g.colW * 0.3f, max(rowH * 0.2f, 1f / zoom)))
            }
        }
    }
}

/**
 * Each lane's playhead from what the engine reports ([reported]:
 * NativeAudio.getPlayheads), or null where its channel isn't playing this
 * lane's sequence.
 */
private fun playheads(arrangement: Arrangement, reported: IntArray): List<Arrangement.Playhead?> {
    val engineTicksPerTick = ENGINE_TICKS_PER_BEAT.toFloat() / arrangement.ticksPerBeat
    return arrangement.lanes.map { lane ->
        val at = lane.channel * NativeAudio.PLAYHEAD_VALUES
        if (at + NativeAudio.PLAYHEAD_VALUES > reported.size) return@map null
        val pattern = reported[at]
        val row = reported[at + 1]
        val entry = reported[at + 2]
        if (pattern < 0 || row < 0 || entry < 0) return@map null
        lane.playhead(
            pattern, entry, row,
            intoRow = reported[at + 3] / engineTicksPerTick,
            sinceStart = reported[at + 4] / engineTicksPerTick,
        )
    }
}
