package io.github.jfuerth.adaptivetracker.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.snapping.SnapPosition
import androidx.compose.foundation.gestures.snapping.rememberSnapFlingBehavior
import androidx.compose.foundation.interaction.DragInteraction
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChange
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import io.github.jfuerth.adaptivetracker.NativeAudio
import io.github.jfuerth.adaptivetracker.editor.Arrangement as SectionArrangement
import io.github.jfuerth.adaptivetracker.Song
import io.github.jfuerth.adaptivetracker.instrument.InstrumentStore
import io.github.jfuerth.adaptivetracker.ui.HoldButton
import io.github.jfuerth.adaptivetracker.ui.NameDialog
import io.github.jfuerth.adaptivetracker.ui.NoteKeyboard
import io.github.jfuerth.adaptivetracker.ui.ToolButton
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.math.abs

private val ROW_HEIGHT = 26.dp
private val MONO = TextStyle(fontFamily = FontFamily.Monospace, fontSize = 15.sp)

/** What the editor's Play button plays. */
private enum class PlayMode(val label: String) { SECTION("Section"), PATTERN("Pattern"), COLUMN("Column") }

/** Horizontal drag distance that moves the cursor by one field. */
private val SWIPE_STEP = 32.dp

/**
 * The edit row: the row whose centre is nearest the centre of the viewport,
 * where the edit bar is. Measured from the laid-out rows, so it matches what
 * is on screen exactly.
 */
private fun centeredRow(state: LazyListState): Int {
    val info = state.layoutInfo
    val centre = (info.viewportStartOffset + info.viewportEndOffset) / 2
    return info.visibleItemsInfo.minByOrNull { abs(it.offset + it.size / 2 - centre) }?.index
        ?: state.firstVisibleItemIndex
}

/**
 * Pattern editor: the pattern scrolls under a fixed edit row in the middle.
 * The 12-key keyboard writes notes (a chord writes an arpeggio); the hex pad
 * writes the effect, or the volume or parameter field if one was tapped.
 */
@Composable
fun EditorScreen(playing: Boolean, modifier: Modifier = Modifier) {
    // Shared with the other screens, so the sequence editor can open a pattern here.
    var patternIndex by Song::pattern
    val pattern = remember(patternIndex, PatternStore.shapes) { PatternStore.get(patternIndex) }
    if (pattern == null) {
        Text("This pattern could not be loaded.", modifier.padding(16.dp))
        return
    }
    val info = pattern.info

    // Kept across patterns (the worksheet moves between them); clamped to this one's columns.
    var column by rememberSaveable { mutableIntStateOf(0) }
    if (column >= info.columns) column = info.columns - 1
    // Arrangement view: the whole section as a worksheet, the cursor in any of its patterns.
    val arranging = Song.arrange
    var sheetRow by rememberSaveable { mutableIntStateOf(0) }
    var sheetInstance by rememberSaveable { mutableLongStateOf(-1L) }
    var field by rememberSaveable { mutableStateOf(Field.NOTE) }
    // Where typing started in the current digit group; the cursor returns
    // here after the group's last digit, ready for the next row.
    var anchor by remember { mutableStateOf<Field?>(null) }
    // MIDI note of the keyboard's leftmost key: octave buttons move it by 12,
    // and dragging the keyboard with Alt held moves it by semitones.
    var firstNote by rememberSaveable { mutableIntStateOf(60) }
    // Alt: held under either thumb, it changes what other controls do.
    var altLeft by remember { mutableStateOf(false) }
    var altRight by remember { mutableStateOf(false) }
    val alt = altLeft || altRight
    var step by rememberSaveable { mutableIntStateOf(1) }
    var follow by rememberSaveable { mutableStateOf(true) }
    var playMode by rememberSaveable { mutableStateOf(PlayMode.SECTION) }

    // Engine state, polled: the key in effect and where this channel is playing.
    val home = remember { NativeAudio.getHomeKey().let { Key(it[0], it[1]) } }
    var currentKey by remember { mutableStateOf(home) }
    var playPos by remember { mutableStateOf(intArrayOf(-1, -1)) }
    val channel by rememberUpdatedState(info.channel)
    LaunchedEffect(Unit) {
        while (true) {
            val k = NativeAudio.getCurrentKey()
            if (k[0] != currentKey.root || k[1] != currentKey.scale) currentKey = Key(k[0], k[1])
            val p = NativeAudio.getPlayPosition(channel)
            if (!p.contentEquals(playPos)) playPos = p
            delay(30)
        }
    }
    val ctx = PitchContext(home, currentKey)
    val playingRow = if (playing && playPos[0] == patternIndex) playPos[1] else -1

    // A fresh scroll state per pattern: the old pattern's position must never
    // be read against the new pattern's rows.
    // Saved, so coming back to this tab finds the same place.
    val listState = rememberSaveable(patternIndex, saver = LazyListState.Saver) { LazyListState() }
    val scope = rememberCoroutineScope()
    val rowPx = with(LocalDensity.current) { ROW_HEIGHT.toPx() }
    // Clamped as well, since the layout can briefly lag the pattern's length.
    val listRow by remember(listState, info.rows) {
        derivedStateOf { centeredRow(listState).coerceIn(0, (info.rows - 1).coerceAtLeast(0)) }
    }
    val editRow = if (arranging) sheetRow.coerceIn(0, (info.rows - 1).coerceAtLeast(0)) else listRow
    // Drags and flings come to rest with a row exactly on the edit bar.
    val snapFling = rememberSnapFlingBehavior(listState, SnapPosition.Center)

    LaunchedEffect(patternIndex) { anchor = null }
    // Dragging the pattern stops following playback.
    LaunchedEffect(listState) {
        listState.interactionSource.interactions.collect { if (it is DragInteraction.Start) follow = false }
    }
    // Following: the list scrolls smoothly with playback, every frame, the
    // playing row passing through the edit bar.
    val playingNow by rememberUpdatedState(playing)
    val engineTicksPerRow by rememberUpdatedState(info.ticksPerRow * 4)  // 96 engine ticks a beat, 24 effect ticks
    // Positions come from a smooth clock (PlaybackClock), not straight from
    // each audio callback, so the scroll rate stays even.
    val playClock = remember { PlaybackClock() }
    val rows by rememberUpdatedState(info.rows)
    LaunchedEffect(follow, patternIndex, listState) {
        if (!follow) return@LaunchedEffect
        while (true) {
            // Where to scroll, worked out in the frame, before it's drawn.
            val target = withFrameNanos { frame ->
                if (!playingNow) {
                    playClock.reset()
                    return@withFrameNanos null
                }
                val snapshot = NativeAudio.getPlayheads()
                val since = playClock.since(frame, snapshot)
                val at = PlaybackClock.CLOCK_VALUES + channel * NativeAudio.PLAYHEAD_VALUES
                if (at + NativeAudio.PLAYHEAD_VALUES > snapshot.size) return@withFrameNanos null
                if (snapshot[at].toInt() != patternIndex || snapshot[at + 1] < 0) return@withFrameNanos null
                // Rows from the top, the playing one moving through the edit bar.
                val position = snapshot[at + 1] + (snapshot[at + 3] + since) / engineTicksPerRow.coerceAtLeast(1)
                position.coerceIn(0.0, rows - 0.001)
            } ?: continue
            if (listState.isScrollInProgress) continue
            val row = target.toInt()
            listState.scrollToItem(row, ((target - row) * rowPx).toInt())
        }
    }
    // Moving to another row ends a digit group the way typing its last digit
    // does: the cursor goes back to where typing started.
    LaunchedEffect(editRow) {
        anchor?.let { field = it }
        anchor = null
    }

    // ---- Playback: the mixer's section, this pattern alone, or one column of it ----
    // Play hears every note; Alt+Play hears the adaptive mix (gates, layer
    // mutes and layer gains), which restarts and mode changes then keep.
    val isPlaying by rememberUpdatedState(playing)
    var hearAll by rememberSaveable { mutableStateOf(true) }
    fun startPlayback() {
        when (playMode) {
            // The section the mixer showed when the editor opened, from its start.
            PlayMode.SECTION -> {
                NativeAudio.setHearAll(hearAll)
                val section = Song.section.takeIf { it in Song.sections.indices } ?: 0
                NativeAudio.gotoSection(section, NativeAudio.Quantize.NOW.ordinal, NativeAudio.Entry.RESTART.ordinal, false)
            }
            PlayMode.PATTERN -> NativeAudio.soloPattern(patternIndex, 0xFFFF, hearAll)
            PlayMode.COLUMN -> NativeAudio.soloPattern(patternIndex, 1 shl column, hearAll)
        }
    }
    // Changing mode or pattern while playing switches what plays; in Column
    // mode, moving to another column switches which column is heard. Opening
    // the tab doesn't change anything that is already playing.
    var lastMode by remember { mutableStateOf(playMode) }
    var lastPattern by remember { mutableIntStateOf(patternIndex) }
    LaunchedEffect(playMode, patternIndex) {
        if (playMode != lastMode || patternIndex != lastPattern) {
            val wasSolo = lastMode != PlayMode.SECTION
            lastMode = playMode
            lastPattern = patternIndex
            if (isPlaying && (playMode != PlayMode.SECTION || wasSolo)) startPlayback()
        }
    }
    LaunchedEffect(column) {
        if (isPlaying && playMode == PlayMode.COLUMN) NativeAudio.setColumnMask(info.channel, 1 shl column)
    }

    // ---- Horizontal swipes move the cursor: note, then each digit, then the next column ----
    // Read through state: the gesture handler lives as long as the editor, so
    // it must see columns added or removed since it started.
    val columnCount by rememberUpdatedState(info.columns)
    fun moveCursor(delta: Int) {
        val fields = Field.entries
        val index = (column * fields.size + field.ordinal + delta).coerceIn(0, columnCount * fields.size - 1)
        column = index / fields.size
        field = fields[index % fields.size]
        anchor = null
    }
    val move by rememberUpdatedState(::moveCursor)

    fun currentCell() = pattern.cell(editRow, column)
    fun write(cell: CellData) = PatternStore.write(patternIndex, editRow, column, cell)
    fun advance(direction: Int = 1) {
        val target = (editRow + direction * step).coerceIn(0, info.rows - 1)
        if (arranging) sheetRow = target else scope.launch { listState.scrollToItem(target) }
    }

    // Preview voices: which key (MIDI note) each of the four slots is sounding.
    val slots = remember { arrayOfNulls<Int>(4) }

    var editingShape by remember { mutableStateOf(false) }
    // The effect reference, open on (row, column).
    var effectAt by remember { mutableStateOf<Pair<Int, Int>?>(null) }
    var confirmClear by remember { mutableStateOf<Int?>(null) }
    var settingsFor by remember { mutableStateOf<Int?>(null) }
    val channelName = Song.channels.getOrElse(info.channel) { NativeAudio.getChannelName(info.channel) }

    if (editingShape) ShapeDialog(pattern, onDone = { change ->
        editingShape = false
        if (change != null) {
            if (!change.scale.isIdentity) PatternStore.scaleRows(patternIndex, change.scale.num, change.scale.den)
            PatternStore.setShape(patternIndex, change.rows, change.ticksPerRow)
        }
    })
    effectAt?.let { (r, c) ->
        if (r < info.rows && c < info.columns) {
            EffectDialog(
                pattern.cell(r, c), info.modeOf(c), Song.events, NativeAudio::effectValue, info.ticksPerBeat,
                onDone = { changed ->
                    effectAt = null
                    if (changed != null) PatternStore.write(patternIndex, r, c, changed)
                },
            )
        } else {
            effectAt = null
        }
    }
    settingsFor?.let { c ->
        ColumnSettingsDialog(info.channel, c, channelName, onDone = { settingsFor = null })
    }
    confirmClear?.let { c ->
        AlertDialog(
            onDismissRequest = { confirmClear = null },
            title = { Text("Clear column ${c + 1}?") },
            text = { Text("Empties all ${info.rows} rows of column ${c + 1} in ${PatternStore.names.getOrElse(patternIndex) { "this pattern" }}.") },
            confirmButton = {
                TextButton(onClick = {
                    confirmClear = null
                    PatternStore.clearCells(patternIndex, c, 0, info.rows - 1)
                }) { Text("Clear") }
            },
            dismissButton = { TextButton(onClick = { confirmClear = null }) { Text("Cancel") } },
        )
    }

    Column(modifier.fillMaxSize()) {
        // ---- Top bar: pattern choice, shape, transport ----
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            var menuOpen by remember { mutableStateOf(false) }
            var renaming by remember { mutableStateOf(false) }
            var full by remember { mutableStateOf(false) }
            var problem by remember { mutableStateOf<String?>(null) }
            var copying by remember { mutableStateOf(false) }
            // A pattern about to be made: its suggested name, and what it copies (-1: empty).
            var creating by remember { mutableStateOf<Pair<String, Int>?>(null) }
            fun unique(name: String) = if (name in PatternStore.names) PatternStore.freshName(name) else name
            if (copying) CopyPatternDialog(info.channel, onDone = { source ->
                copying = false
                if (source != null) creating = unique(PatternStore.names[source]) to source
            })
            creating?.let { (suggested, source) ->
                NameDialog(if (source < 0) "New $channelName pattern" else "Copy of a pattern", suggested, "Add") { name ->
                    creating = null
                    if (name != null) {
                        val i = PatternStore.create(info.channel, name, source)
                        if (i >= 0) patternIndex = i else full = true
                    }
                }
            }
            problem?.let {
                AlertDialog(
                    onDismissRequest = { problem = null },
                    text = { Text(it) },
                    confirmButton = { TextButton(onClick = { problem = null }) { Text("OK") } },
                )
            }
            if (renaming) NameDialog("Rename pattern", PatternStore.names.getOrElse(patternIndex) { "" }) { name ->
                renaming = false
                if (name != null) PatternStore.rename(patternIndex, name)
            }
            if (full) AlertDialog(
                onDismissRequest = { full = false },
                title = { Text("No room for another pattern") },
                text = { Text("A song can have up to 64 patterns.") },
                confirmButton = { TextButton(onClick = { full = false }) { Text("OK") } },
            )
            Box {
                TextButton(onClick = { menuOpen = true }) {
                    Text("${PatternStore.names.getOrElse(patternIndex) { "?" }} ▾")
                }
                DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                    DropdownMenuItem(text = { Text("New $channelName pattern…") }, onClick = {
                        menuOpen = false
                        creating = PatternStore.freshName(channelName) to -1
                    })
                    DropdownMenuItem(text = { Text("Duplicate this pattern…") }, onClick = {
                        menuOpen = false
                        val base = PatternStore.names.getOrElse(patternIndex) { channelName }
                        creating = unique("$base copy") to patternIndex
                    })
                    DropdownMenuItem(
                        text = { Text(if (arranging) "Back to the pattern view" else "Arrangement view: the whole section") },
                        onClick = {
                            menuOpen = false
                            if (!arranging) {
                                sheetRow = editRow
                                sheetInstance = -1L
                            } else {
                                scope.launch { listState.scrollToItem(sheetRow.coerceIn(0, info.rows - 1)) }
                            }
                            Song.arrange = !arranging
                        },
                    )
                    DropdownMenuItem(text = { Text("Rename this pattern…") }, onClick = {
                        menuOpen = false
                        renaming = true
                    })
                    DropdownMenuItem(text = { Text("Delete this pattern") }, onClick = {
                        menuOpen = false
                        problem = Song.delete(NativeAudio.NAMES_PATTERNS, patternIndex)
                    })
                    if (Song.channels.size > 1) {
                        DropdownMenuItem(text = { Text("Copy from another channel…") }, onClick = {
                            menuOpen = false
                            copying = true
                        })
                    }
                    HorizontalDivider()
                    // Only this channel's patterns: a pattern plays on its own channel.
                    val mine = PatternStore.ofChannel(info.channel).toSet()
                    PatternStore.byName.filter { it in mine }.forEach { i ->
                        val name = PatternStore.names[i]
                        val shape = PatternStore.get(i)?.info?.let { " · ${it.rows} rows, speed ${it.speedLabel}" } ?: ""
                        DropdownMenuItem(text = { Text(name + shape) }, onClick = {
                            patternIndex = i
                            menuOpen = false
                        })
                    }
                }
            }
            Box(Modifier.weight(1f))
            // One button for what Play plays: tapping it goes to the next choice.
            ToolButton(playMode.label, onClick = {
                playMode = PlayMode.entries[(playMode.ordinal + 1) % PlayMode.entries.size]
            })
            Box(Modifier.width(4.dp))
            ToolButton("Follow", selected = follow, onClick = { follow = !follow })
            Box(Modifier.width(4.dp))
            ToolButton(
                when {
                    playing -> "Stop"
                    alt -> "Play mix"
                    else -> "Play"
                },
                selected = !playing && alt,
                onClick = {
                    if (playing) NativeAudio.stop() else {
                        hearAll = !alt
                        startPlayback()
                    }
                },
            )
        }

        if (arranging) {
            // ---- The section as a worksheet ----
            val section = Song.section.takeIf { it in Song.sections.indices } ?: 0
            val arrangement = remember(section, Song.revision, PatternStore.shapes, Song.channels) {
                val timing = NativeAudio.getSectionTiming(section)
                SectionArrangement.build(
                    sequences = Song.channels.indices.map { NativeAudio.getSequence(section, it)?.toList() ?: emptyList() },
                    columns = Song.channels.indices.map { ch ->
                        PatternStore.ofChannel(ch).firstNotNullOfOrNull { PatternStore.get(it)?.info?.columns } ?: 1
                    },
                    info = { PatternStore.get(it)?.info },
                    ticksPerBeat = info.ticksPerBeat,
                    beatsPerBar = timing?.getOrNull(1) ?: 4,
                )
            }
            // Where the cursor's pattern plays: the instance last tapped, else its first.
            val lane = arrangement.lanes.indexOfFirst { it.channel == info.channel }
            val instance = arrangement.lanes.getOrNull(lane)?.let { l ->
                l.firstOf(patternIndex, sheetInstance.coerceAtLeast(0))?.start
            } ?: -1L
            val cursor = if (lane >= 0 && instance >= 0) SheetCursor(lane, instance, patternIndex, editRow, column, field) else null
            Worksheet(
                arrangement, ctx, Song.channels, cursor, playing, follow,
                onUserMove = { follow = false },
                onTap = { t ->
                    if (cursor != null && t == cursor && t.field.isEffect) {
                        effectAt = t.row to t.column  // tapping the cursor itself: the effect reference
                    } else {
                        patternIndex = t.pattern
                        sheetInstance = t.instance
                        sheetRow = t.row
                        column = t.column
                        field = t.field
                    }
                    anchor = null
                },
                modifier = Modifier.weight(1f).fillMaxWidth().clipToBounds(),
            )
        } else {
        // ---- Column headings: the instrument each column plays ----
        val measurer = rememberTextMeasurer()
        val density = LocalDensity.current
        val columnWidth = remember(measurer, density) {
            with(density) { measurer.measure("C-4 .. ...", MONO).size.width.toDp() } + 12.dp
        }
        ColumnHeadings(
            info, channelName, column, columnWidth,
            onSelect = { c -> column = c; anchor = null },
            onShape = { editingShape = true },
            onSettings = { c -> settingsFor = c },
            onClear = { c -> confirmClear = c },
            onAdd = {
                if (PatternStore.setColumns(info.channel, info.columns + 1)) InstrumentStore.columnsChanged()
            },
            onRemove = { c ->
                val remaining = info.columns - 1
                if (column >= remaining) column = remaining - 1
                anchor = null
                if (PatternStore.removeColumn(info.channel, c)) InstrumentStore.columnsChanged()
            },
        )

        // ---- Pattern ----
        var viewportPx by remember { mutableIntStateOf(0) }
        val padding = with(LocalDensity.current) {
            ((viewportPx - rowPx) / 2f).coerceAtLeast(0f).toDp()
        }
        val scrolling = listState.isScrollInProgress
        Box(
            Modifier
                .weight(1f)
                .fillMaxWidth()
                .onSizeChanged { viewportPx = it.height }
                .pointerInput(Unit) {
                    // Seen before the rows (Initial pass): a drag that goes
                    // sideways first moves the cursor, and the rows don't
                    // scroll until the finger lifts. One that goes up or down
                    // first is left to scroll them.
                    val stepPx = SWIPE_STEP.toPx()
                    val slop = viewConfiguration.touchSlop
                    awaitEachGesture {
                        val down = awaitFirstDown(requireUnconsumed = false, pass = PointerEventPass.Initial)
                        var dx = 0f
                        var dy = 0f
                        var sideways = false
                        var accumulated = 0f
                        while (true) {
                            val event = awaitPointerEvent(PointerEventPass.Initial)
                            val change = event.changes.firstOrNull { it.id == down.id } ?: break
                            if (!change.pressed) break
                            val delta = change.positionChange()
                            if (!sideways) {
                                dx += delta.x
                                dy += delta.y
                                if (abs(dy) > slop && abs(dy) >= abs(dx)) break  // scrolling
                                if (abs(dx) <= slop || abs(dx) <= abs(dy)) continue
                                sideways = true
                            } else {
                                accumulated += delta.x
                            }
                            event.changes.forEach { it.consume() }
                            while (accumulated >= stepPx) {
                                move(1)
                                accumulated -= stepPx
                            }
                            while (accumulated <= -stepPx) {
                                move(-1)
                                accumulated += stepPx
                            }
                        }
                    }
                },
        ) {
            // The edit bar stays fixed in the middle; rows scroll past it.
            Box(
                Modifier
                    .align(Alignment.Center)
                    .fillMaxWidth()
                    .height(ROW_HEIGHT)
                    .background(MaterialTheme.colorScheme.primaryContainer),
            )
            LazyColumn(
                state = listState,
                flingBehavior = snapFling,
                contentPadding = PaddingValues(vertical = padding),
                modifier = Modifier.fillMaxSize(),
            ) {
                items(info.rows) { row ->
                    PatternRow(
                        row = row,
                        pattern = pattern,
                        ctx = ctx,
                        isEdit = row == editRow && !scrolling,
                        isPlaying = row == playingRow,
                        selectedColumn = column,
                        selectedField = field,
                        columnWidth = columnWidth,
                        onTap = { c, f ->
                            // Tapping the cursor itself, on an effect digit, opens the effect reference.
                            if (row == editRow && c == column && f == field && f.isEffect) {
                                effectAt = row to c
                            } else {
                                column = c
                                field = f
                                if (row != editRow) scope.launch { listState.scrollToItem(row) }
                            }
                            anchor = null
                        },
                    )
                }
            }
        }
        }

        // ---- Description line ----
        val cell = currentCell()
        val description = when {
            alt -> "Drag the keyboard to move it · Del goes up; hold it to clear to the top · Pull moves rows up · Play the mix"
            field.isVolume -> if (cell.volume == CellData.VOLUME_DEFAULT) "Volume · default (full)"
                else "Volume · ${cell.volume}/127"
            else -> EffectDescription.describe(cell, info.modeOf(column), Song.events, NativeAudio::effectValue, info.ticksPerBeat)
        }
        // Help line, with an Alt key at each end over the 0 and 7 keys, under the thumbs.
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 4.dp, vertical = 2.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            HoldButton("Alt", held = altLeft, onHeld = { altLeft = it }, modifier = Modifier.weight(1f).padding(horizontal = 2.dp))
            Text(
                description,
                style = MaterialTheme.typography.bodySmall,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(6f).padding(horizontal = 6.dp),
            )
            HoldButton("Alt", held = altRight, onHeld = { altRight = it }, modifier = Modifier.weight(1f).padding(horizontal = 2.dp))
        }

        // ---- Hex pad ----
        // A digit goes where the cursor is (the effect command when on the note)
        // and the cursor moves to the next digit of the group. After the last
        // digit it returns to where typing started and the row advances, so
        // selecting the last digit and typing tweaks just that digit.
        HexPad(onDigit = { d ->
            val start = anchor ?: field
            val target = if (field == Field.NOTE) Field.EFFECT else field
            write(Editing.typeHex(currentCell(), target, d))
            val next = target.nextDigit
            if (next != null) {
                anchor = start
                field = next
            } else {
                field = start
                anchor = null
                advance()
            }
        })

        // ---- Keyboard ----
        val scale = currentKey.scale
        val root = currentKey.root
        NoteKeyboard(
            firstNote = firstNote,
            dimNote = { n -> info.modeOf(column) == PitchMode.DEGREE && !Pitch.inScale(scale, n - root) },
            onNoteDown = { n ->
                val free = slots.indexOfFirst { it == null }
                if (free >= 0) {
                    slots[free] = n
                    NativeAudio.previewOn(NativeAudio.getColumnInstrument(info.channel, column), free, n)
                }
            },
            onNoteUp = { k ->
                val s = slots.indexOf(k)
                if (s >= 0) {
                    slots[s] = null
                    NativeAudio.previewOff(s)
                }
            },
            onChord = { keys ->
                Editing.chordCell(keys, info.modeOf(column), ctx, currentCell())?.let {
                    write(it)
                    advance()
                }
            },
            modifier = Modifier.fillMaxWidth().height(104.dp).padding(horizontal = 4.dp),
            sliding = alt,
            onSlide = { by -> firstNote = (firstNote + by).coerceIn(0, MAX_FIRST_NOTE) },
        )

        // ---- Toolbar ----
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 4.dp, vertical = 4.dp),
            horizontalArrangement = Arrangement.spacedBy(4.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            val narrow = 7.dp
            ToolButton("−", horizontalPadding = narrow, onClick = { firstNote = (firstNote - 12).coerceAtLeast(0) })
            Text(Pitch.noteName(firstNote), style = MONO.copy(fontSize = 13.sp))
            ToolButton("+", horizontalPadding = narrow, onClick = { firstNote = (firstNote + 12).coerceAtMost(MAX_FIRST_NOTE) })
            Box(Modifier.width(2.dp))
            ToolButton("−", horizontalPadding = narrow, onClick = { step = (step - 1).coerceAtLeast(0) })
            Text("Step $step", style = MONO.copy(fontSize = 13.sp))
            ToolButton("+", horizontalPadding = narrow, onClick = { step = (step + 1).coerceAtMost(16) })
            Box(Modifier.weight(1f))
            ToolButton("Off", onClick = {
                write(currentCell().copy(note = CellData.NOTE_OFF, accidental = 0))
                advance()
            })
            // Del clears the field under the cursor and moves on (up with Alt).
            // Held, it clears that field to the end of the column (the start with Alt).
            ToolButton(
                if (alt) "Del↑" else "Del",
                selected = alt,
                onClick = {
                    write(Editing.clear(currentCell(), field))
                    anchor = null
                    advance(if (alt) -1 else 1)
                },
                onLongClick = {
                    anchor = null
                    val (first, last) = if (alt) 0 to editRow else editRow to info.rows - 1
                    PatternStore.clearCells(patternIndex, column, first, last, Editing.parts(field))
                },
            )
            // Ins pushes the edit row and those below it down one row, in this
            // column; with Alt it pulls them up instead. Held: every column.
            // The edit row stays put, so repeated taps keep moving the same notes.
            ToolButton(
                if (alt) "Pull" else "Ins",
                selected = alt,
                onClick = {
                    anchor = null
                    PatternStore.shiftRows(patternIndex, 1 shl column, editRow, down = !alt)
                },
                onLongClick = {
                    anchor = null
                    PatternStore.shiftRows(patternIndex, (1 shl info.columns) - 1, editRow, down = !alt)
                },
            )
        }
    }
}

/**
 * One heading per column, lined up over the cells, naming the instrument the
 * column plays. Tapping a heading selects the column and offers the song's
 * instruments for it, and column actions. A "+" after the last adds a column.
 * Columns belong to the channel, so adding or removing one changes all of
 * the channel's patterns. Over the row numbers, the pattern's length: tapping
 * it opens the length and speed dialog.
 */
@Composable
private fun ColumnHeadings(
    info: PatternInfo,
    channelName: String,
    selectedColumn: Int,
    columnWidth: Dp,
    onSelect: (Int) -> Unit,
    onShape: () -> Unit,
    onSettings: (Int) -> Unit,
    onClear: (Int) -> Unit,
    onAdd: () -> Unit,
    onRemove: (Int) -> Unit,
) {
    val colors = MaterialTheme.colorScheme
    val names = InstrumentStore.names
    InstrumentStore.assignments  // read, so a new assignment redraws the headings
    Row(Modifier.fillMaxWidth().padding(horizontal = 6.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(
            "${info.rows}▾",
            style = MaterialTheme.typography.labelSmall,
            color = colors.onSecondaryContainer,
            maxLines = 1,
            textAlign = TextAlign.Center,
            modifier = Modifier
                .width(30.dp)
                .clip(RoundedCornerShape(4.dp))
                .background(colors.secondaryContainer)
                .clickable(onClick = onShape)
                .padding(vertical = 4.dp),
        )
        for (c in 0 until info.columns) {
            val current = NativeAudio.getColumnInstrument(info.channel, c)
            var open by remember { mutableStateOf(false) }
            Box(Modifier.padding(start = 6.dp).width(columnWidth)) {
                Text(
                    (names.getOrNull(current) ?: "—") + " ▾",
                    style = MaterialTheme.typography.labelSmall,
                    color = if (c == selectedColumn) colors.primary else colors.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(4.dp))
                        .clickable { onSelect(c); open = true }
                        .padding(horizontal = 2.dp, vertical = 4.dp),
                )
                DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
                    Text(
                        "Instrument for column ${c + 1} in every $channelName pattern",
                        style = MaterialTheme.typography.labelMedium,
                        color = colors.onSurfaceVariant,
                        modifier = Modifier.padding(horizontal = 12.dp, vertical = 4.dp).width(220.dp),
                    )
                    names.forEachIndexed { i, name ->
                        DropdownMenuItem(
                            text = { Text(if (i == current) "✓ $name" else name) },
                            onClick = {
                                InstrumentStore.assign(info.channel, c, i)
                                open = false
                            },
                        )
                    }
                    HorizontalDivider()
                    DropdownMenuItem(text = { Text("Column ${c + 1} settings…") }, onClick = {
                        open = false
                        onSettings(c)
                    })
                    DropdownMenuItem(text = { Text("Clear column ${c + 1}…") }, onClick = {
                        open = false
                        onClear(c)
                    })
                    DropdownMenuItem(
                        text = { Text("Remove column ${c + 1} from all $channelName patterns") },
                        enabled = info.columns > 1,
                        onClick = {
                            open = false
                            onRemove(c)
                        },
                    )
                }
            }
        }
        if (info.columns < MAX_COLUMNS) {
            var open by remember { mutableStateOf(false) }
            Box(Modifier.padding(start = 6.dp)) {
                Text(
                    "+",
                    style = MaterialTheme.typography.labelLarge,
                    color = colors.primary,
                    modifier = Modifier
                        .clip(RoundedCornerShape(4.dp))
                        .clickable { open = true }
                        .padding(horizontal = 8.dp, vertical = 2.dp),
                )
                DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
                    DropdownMenuItem(text = { Text("Add a column to all $channelName patterns") }, onClick = {
                        open = false
                        onAdd()
                    })
                }
            }
        }
    }
}

/** Highest note the keyboard can start on, so its 12 keys stay within MIDI. */
private const val MAX_FIRST_NOTE = 127 - 11

/** Columns a channel can have (the engine's kMaxColumns). */
private const val MAX_COLUMNS = 4

/** Speeds that divide a beat evenly, with what they're called. */
private val SPEED_PRESETS = listOf(3 to "8/beat", 6 to "4/beat", 8 to "3/beat", 12 to "2/beat", 24 to "1/beat")

/** What the length and speed dialog asks for: stretch or squash first, then set the shape. */
private data class ShapeChange(val scale: RowScale, val rows: Int, val ticksPerRow: Int)

/**
 * Edits a pattern's length (rows) and speed (ticks per row), and can stretch
 * or squash its rows. Returns the change, or null if cancelled.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ShapeDialog(pattern: PatternStore.Loaded, onDone: (ShapeChange?) -> Unit) {
    val info = pattern.info
    var rowsText by remember { mutableStateOf(info.rows.toString()) }
    var speedText by remember { mutableStateOf(info.ticksPerRow.toString()) }
    var scale by remember { mutableStateOf(RowScale()) }
    val rows = rowsText.toIntOrNull()?.takeIf { it in 1..MAX_ROWS }
    val speed = speedText.toIntOrNull()?.takeIf { it in 1..255 }
    val scaledRows = scale.rows(info.rows)

    // Cells a squash would lose: the rows that land between rows.
    val dropped = remember(scale) {
        (0 until info.rows).filter { scale.drops(it) }
            .sumOf { r -> (0 until info.columns).count { c -> !pattern.cell(r, c).isEmpty } }
    }

    AlertDialog(
        onDismissRequest = { onDone(null) },
        title = { Text("Length and speed") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Stepper("Rows (1–$MAX_ROWS)", rowsText, { rowsText = it }, rows, 1..MAX_ROWS,
                    listOf("−" to { v -> v - 1 }, "+" to { v -> v + 1 }, "×2 append" to { v -> v * 2 }))
                Text("Move the rows", style = MaterialTheme.typography.labelLarge)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    // Each applies to the rows as they'd be after the ones before.
                    fun apply(next: RowScale) {
                        scale = next
                        rowsText = next.rows(info.rows).toString()
                    }
                    listOf(2, 3).forEach { k ->
                        TextButton(onClick = { apply(scale.stretch(k)) }, enabled = scaledRows * k <= MAX_ROWS) {
                            Text("×$k stretch")
                        }
                    }
                    listOf(2, 3).forEach { k ->
                        TextButton(onClick = { apply(scale.squash(k)) }, enabled = scaledRows >= k) {
                            Text("÷$k squash")
                        }
                    }
                    if (!scale.isIdentity) TextButton(onClick = { apply(RowScale()) }) { Text("Undo") }
                }
                if (!scale.isIdentity) {
                    val how = if (scale.den == 1) (if (scale.num == 2) "an empty row goes" else "${scale.num - 1} empty rows go") + " after each row"
                        else if (scale.num == 1) "every ${ordinal(scale.den)} row is kept"
                        else "row r moves to r × ${scale.num}/${scale.den}"
                    val loss = if (dropped > 0) " · $dropped cells on the rows between are deleted" else ""
                    Text("Rows: $how$loss. The speed stays as it is, so the pattern plays at a different pace unless you change it below.",
                        style = MaterialTheme.typography.bodySmall,
                        color = if (dropped > 0) MaterialTheme.colorScheme.error else Color.Unspecified)
                }
                Stepper("Speed: ticks per row (${info.ticksPerBeat} per beat)", speedText, { speedText = it }, speed,
                    1..255, listOf("−" to { v -> v - 1 }, "+" to { v -> v + 1 }))
                FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    SPEED_PRESETS.forEach { (tpr, label) ->
                        FilterChip(selected = speed == tpr, onClick = { speedText = tpr.toString() }, label = { Text(label) })
                    }
                }
                if (rows != null && speed != null && info.ticksPerBeat > 0) {
                    val beats = "%.2f".format(rows * speed / info.ticksPerBeat.toFloat()).trimEnd('0').trimEnd('.')
                    Text(
                        "Speed ${info.copy(ticksPerRow = speed).speedLabel} · $beats beats long",
                        style = MaterialTheme.typography.bodySmall,
                    )
                }
                if (rows != null && rows < scaledRows) {
                    Text(
                        "Rows past the new end are hidden, not deleted, while the app stays open: make the pattern longer again to bring them back. Saved songs leave them out.",
                        style = MaterialTheme.typography.bodySmall,
                    )
                }
            }
        },
        confirmButton = {
            TextButton(
                onClick = { if (rows != null && speed != null) onDone(ShapeChange(scale, rows, speed)) },
                enabled = rows != null && speed != null,
            ) { Text("Set") }
        },
        dismissButton = { TextButton(onClick = { onDone(null) }) { Text("Cancel") } },
    )
}

private const val MAX_ROWS = 256

private fun ordinal(n: Int): String = when (n) {
    2 -> "2nd"
    3 -> "3rd"
    else -> "${n}th"
}

/** A number field with buttons that change it ([steps]: label to new value), kept within [range]. */
@Composable
private fun Stepper(
    label: String,
    text: String,
    onText: (String) -> Unit,
    value: Int?,
    range: IntRange,
    steps: List<Pair<String, (Int) -> Int>>,
) {
    Text(label, style = MaterialTheme.typography.labelLarge)
    Row(verticalAlignment = Alignment.CenterVertically) {
        OutlinedTextField(
            text,
            onValueChange = { t -> onText(t.filter { it.isDigit() }.take(3)) },
            singleLine = true,
            isError = value == null,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
            modifier = Modifier.width(80.dp),
        )
        steps.forEach { (name, f) ->
            TextButton(
                onClick = { onText(f(value ?: range.first).coerceIn(range).toString()) },
                contentPadding = PaddingValues(horizontal = 6.dp),
            ) { Text(name) }
        }
    }
}

@Composable
private fun PatternRow(
    row: Int,
    pattern: PatternStore.Loaded,
    ctx: PitchContext,
    isEdit: Boolean,
    isPlaying: Boolean,
    selectedColumn: Int,
    selectedField: Field,
    columnWidth: Dp,
    onTap: (column: Int, field: Field) -> Unit,
) {
    val colors = MaterialTheme.colorScheme
    val info = pattern.info
    // The edit bar is drawn behind the list (fixed in the viewport), so rows
    // are transparent. The playing row is bold (a tint would smear as the list
    // scrolls smoothly with playback).
    val cellStyle = if (isPlaying) MONO.copy(fontWeight = FontWeight.Bold) else MONO
    val onBeat = info.isBeat(row)
    Row(
        Modifier.fillMaxWidth().height(ROW_HEIGHT).padding(horizontal = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            "%02d".format(row),
            style = MONO.copy(fontWeight = if (onBeat) FontWeight.Bold else FontWeight.Normal),
            color = if (onBeat) colors.primary else colors.onSurfaceVariant,
            modifier = Modifier.width(30.dp),
        )
        for (c in 0 until info.columns) {
            val cell = pattern.cell(row, c)
            @Composable
            fun part(text: String, f: Field) {
                val selected = isEdit && c == selectedColumn && f == selectedField
                Text(
                    text,
                    style = cellStyle,
                    color = if (selected) colors.onSecondary else colors.onSurface,
                    modifier = Modifier
                        .background(if (selected) colors.secondary else Color.Transparent)
                        .clickable { onTap(c, f) }
                        .padding(horizontal = 1.dp),
                )
            }
            Row(Modifier.padding(start = 6.dp).width(columnWidth)) {
                part(CellFormat.note(cell, info.modeOf(c), ctx), Field.NOTE)
                Text(" ", style = MONO)
                val vol = CellFormat.volume(cell)
                part(vol.substring(0, 1), Field.VOLUME_HI)
                part(vol.substring(1, 2), Field.VOLUME_LO)
                Text(" ", style = MONO)
                val fx = CellFormat.effect(cell)
                part(fx.substring(0, 1), Field.EFFECT)
                part(fx.substring(1, 2), Field.PARAM_X)
                part(fx.substring(2, 3), Field.PARAM_Y)
            }
        }
    }
}

@Composable
private fun HexPad(onDigit: (Int) -> Unit) {
    val colors = MaterialTheme.colorScheme
    Column(Modifier.fillMaxWidth().padding(horizontal = 4.dp)) {
        for (r in 0..1) {
            Row(Modifier.fillMaxWidth()) {
                for (c in 0..7) {
                    val digit = r * 8 + c
                    Box(
                        Modifier
                            .weight(1f)
                            .height(40.dp)
                            .padding(2.dp)
                            .clip(RoundedCornerShape(6.dp))
                            .background(colors.secondaryContainer)
                            .clickable { onDigit(digit) },
                        contentAlignment = Alignment.Center,
                    ) {
                        Text("%X".format(digit), style = MONO, color = colors.onSecondaryContainer)
                    }
                }
            }
        }
    }
}

/**
 * Picks a pattern of another channel to copy into [channel]: the patterns,
 * under their channels' names. Notes are rewritten if the two channels
 * write them differently (scale degrees or note names), so the copy sounds
 * the same. [onDone] gets the pattern chosen, or null.
 */
@Composable
private fun CopyPatternDialog(channel: Int, onDone: (Int?) -> Unit) {
    val byChannel = remember {
        val sorted = PatternStore.byName
        Song.channels.indices.filter { it != channel }.associateWith { c ->
            val theirs = PatternStore.ofChannel(c).toSet()
            sorted.filter { it in theirs }
        }.filterValues { it.isNotEmpty() }
    }
    AlertDialog(
        onDismissRequest = { onDone(null) },
        title = { Text("Copy a pattern into ${Song.channels.getOrElse(channel) { "this channel" }}") },
        text = {
            LazyColumn {
                byChannel.forEach { (c, patterns) ->
                    item {
                        Text(
                            Song.channels.getOrElse(c) { "?" },
                            style = MaterialTheme.typography.labelLarge,
                            color = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.padding(top = 8.dp, bottom = 2.dp),
                        )
                    }
                    items(patterns.size) { k ->
                        val i = patterns[k]
                        Text(
                            PatternStore.names.getOrElse(i) { "?" },
                            modifier = Modifier.fillMaxWidth().clickable { onDone(i) }.padding(vertical = 8.dp, horizontal = 4.dp),
                        )
                    }
                }
            }
        },
        confirmButton = {},
        dismissButton = { TextButton(onClick = { onDone(null) }) { Text("Cancel") } },
    )
}
