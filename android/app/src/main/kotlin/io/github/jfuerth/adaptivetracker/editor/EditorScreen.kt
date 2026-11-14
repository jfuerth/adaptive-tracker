package io.github.jfuerth.adaptivetracker.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import io.github.jfuerth.adaptivetracker.NativeAudio
import io.github.jfuerth.adaptivetracker.Song
import io.github.jfuerth.adaptivetracker.instrument.InstrumentStore
import io.github.jfuerth.adaptivetracker.ui.NoteKeyboard
import io.github.jfuerth.adaptivetracker.ui.ToolButton
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.math.abs

private val ROW_HEIGHT = 26.dp
private val MONO = TextStyle(fontFamily = FontFamily.Monospace, fontSize = 15.sp)

/** What the editor's Play button plays. */
private enum class PlayMode(val label: String) { SONG("Song"), PATTERN("Pattern"), COLUMN("Column") }

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
    var patternIndex by rememberSaveable { mutableIntStateOf(0) }
    val pattern = remember(patternIndex, PatternStore.shapes) { PatternStore.get(patternIndex) }
    if (pattern == null) {
        Text("This pattern could not be loaded.", modifier.padding(16.dp))
        return
    }
    val info = pattern.info

    var column by rememberSaveable(patternIndex) { mutableIntStateOf(0) }
    var field by rememberSaveable { mutableStateOf(Field.NOTE) }
    // Where typing started in the current digit group; the cursor returns
    // here after the group's last digit, ready for the next row.
    var anchor by remember { mutableStateOf<Field?>(null) }
    var octave by rememberSaveable { mutableIntStateOf(4) }
    var step by rememberSaveable { mutableIntStateOf(1) }
    var follow by rememberSaveable { mutableStateOf(true) }
    var playMode by rememberSaveable { mutableStateOf(PlayMode.SONG) }

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
    val listState = remember(patternIndex) { LazyListState() }
    val scope = rememberCoroutineScope()
    val rowPx = with(LocalDensity.current) { ROW_HEIGHT.toPx() }
    // Clamped as well, since the layout can briefly lag the pattern's length.
    val editRow by remember(listState, info.rows) {
        derivedStateOf { centeredRow(listState).coerceIn(0, (info.rows - 1).coerceAtLeast(0)) }
    }
    // Drags and flings come to rest with a row exactly on the edit bar.
    val snapFling = rememberSnapFlingBehavior(listState, SnapPosition.Center)

    LaunchedEffect(patternIndex) { anchor = null }
    // Dragging the pattern stops following playback.
    LaunchedEffect(listState) {
        listState.interactionSource.interactions.collect { if (it is DragInteraction.Start) follow = false }
    }
    LaunchedEffect(playingRow, follow) {
        if (follow && playingRow >= 0 && !listState.isScrollInProgress) listState.scrollToItem(playingRow)
    }
    LaunchedEffect(editRow) { anchor = null }

    // ---- Playback: song, this pattern alone, or one column of it ----
    val isPlaying by rememberUpdatedState(playing)
    fun startPlayback() {
        when (playMode) {
            PlayMode.SONG -> NativeAudio.play()
            PlayMode.PATTERN -> NativeAudio.soloPattern(patternIndex, 0xFFFF)
            PlayMode.COLUMN -> NativeAudio.soloPattern(patternIndex, 1 shl column)
        }
    }
    // Changing mode or pattern while playing switches what plays; in Column
    // mode, moving to another column switches which column is heard. Opening
    // the tab doesn't change anything that is already playing.
    var lastMode by remember { mutableStateOf(playMode) }
    var lastPattern by remember { mutableIntStateOf(patternIndex) }
    LaunchedEffect(playMode, patternIndex) {
        if (playMode != lastMode || patternIndex != lastPattern) {
            val wasSolo = lastMode != PlayMode.SONG
            lastMode = playMode
            lastPattern = patternIndex
            if (isPlaying && (playMode != PlayMode.SONG || wasSolo)) startPlayback()
        }
    }
    LaunchedEffect(column) {
        if (isPlaying && playMode == PlayMode.COLUMN) NativeAudio.setColumnMask(info.channel, 1 shl column)
    }

    // ---- Horizontal swipes move the cursor: note, then each digit, then the next column ----
    fun moveCursor(delta: Int) {
        val fields = Field.entries
        val index = (column * fields.size + field.ordinal + delta).coerceIn(0, info.columns * fields.size - 1)
        column = index / fields.size
        field = fields[index % fields.size]
        anchor = null
    }
    val move by rememberUpdatedState(::moveCursor)

    fun currentCell() = pattern.cell(editRow, column)
    fun write(cell: CellData) = PatternStore.write(patternIndex, editRow, column, cell)
    fun advance() {
        val target = (editRow + step).coerceAtMost(info.rows - 1)
        scope.launch { listState.scrollToItem(target) }
    }

    // Preview voices: which key (MIDI note) each of the four slots is sounding.
    val slots = remember { arrayOfNulls<Int>(4) }
    fun midiOf(key: Int) = ((octave + 1) * 12 + key).coerceIn(0, 127)

    var editingShape by remember { mutableStateOf(false) }
    var confirmClear by remember { mutableStateOf<Int?>(null) }
    val channelName = remember(info.channel) { NativeAudio.getChannelName(info.channel) }

    if (editingShape) ShapeDialog(info, onDone = { shape ->
        editingShape = false
        if (shape != null) PatternStore.setShape(patternIndex, shape.first, shape.second)
    })
    confirmClear?.let { c ->
        AlertDialog(
            onDismissRequest = { confirmClear = null },
            title = { Text("Clear column ${c + 1}?") },
            text = { Text("Empties all ${info.rows} rows of column ${c + 1} in ${PatternStore.names.getOrElse(patternIndex) { "this pattern" }}.") },
            confirmButton = {
                TextButton(onClick = {
                    confirmClear = null
                    PatternStore.clearColumn(patternIndex, c)
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
            Box {
                TextButton(onClick = { menuOpen = true }) {
                    Text("${PatternStore.names.getOrElse(patternIndex) { "?" }} ▾")
                }
                DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                    DropdownMenuItem(
                        text = { Text("Length and speed…  (${info.rows} rows, speed ${info.ticksPerRow})") },
                        onClick = {
                            menuOpen = false
                            editingShape = true
                        },
                    )
                    HorizontalDivider()
                    PatternStore.names.forEachIndexed { i, name ->
                        val shape = PatternStore.get(i)?.info?.let { " · ${it.rows} rows, speed ${it.speedLabel}" } ?: ""
                        DropdownMenuItem(text = { Text(name + shape) }, onClick = {
                            patternIndex = i
                            menuOpen = false
                        })
                    }
                }
            }
            Box(Modifier.weight(1f))
            PlayMode.entries.forEach { m ->
                ToolButton(m.label, selected = playMode == m, onClick = { playMode = m })
                Box(Modifier.width(4.dp))
            }
            ToolButton(if (playing) "Stop" else "Play", onClick = {
                if (playing) NativeAudio.stop() else startPlayback()
            })
        }

        // ---- Column headings: the instrument each column plays ----
        val measurer = rememberTextMeasurer()
        val density = LocalDensity.current
        val columnWidth = remember(measurer, density) {
            with(density) { measurer.measure("C-4 .. ...", MONO).size.width.toDp() } + 12.dp
        }
        ColumnHeadings(
            info, channelName, column, columnWidth,
            onSelect = { c -> column = c; anchor = null },
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
                    val stepPx = SWIPE_STEP.toPx()
                    var accumulated = 0f
                    detectHorizontalDragGestures(onDragStart = { accumulated = 0f }) { change, dx ->
                        change.consume()
                        accumulated += dx
                        while (accumulated >= stepPx) {
                            move(1)
                            accumulated -= stepPx
                        }
                        while (accumulated <= -stepPx) {
                            move(-1)
                            accumulated += stepPx
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
                            column = c
                            field = f
                            anchor = null
                            if (row != editRow) scope.launch { listState.scrollToItem(row) }
                        },
                    )
                }
            }
        }

        // ---- Description line ----
        val cell = currentCell()
        val description = when {
            field.isVolume -> if (cell.volume == CellData.VOLUME_DEFAULT) "Volume · default (full)"
                else "Volume · ${cell.volume}/127"
            else -> EffectDescription.describe(cell, info.mode, Song.events, NativeAudio::effectValue, info.ticksPerBeat)
        }
        Text(
            description,
            style = MaterialTheme.typography.bodySmall,
            maxLines = 1,
            modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 4.dp),
        )

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
            dimKey = { k -> info.mode == PitchMode.DEGREE && !Pitch.inScale(scale, k - root) },
            onKeyDown = { k ->
                val free = slots.indexOfFirst { it == null }
                if (free >= 0) {
                    slots[free] = k
                    NativeAudio.previewOn(NativeAudio.getColumnInstrument(info.channel, column), free, midiOf(k))
                }
            },
            onKeyUp = { k ->
                val s = slots.indexOf(k)
                if (s >= 0) {
                    slots[s] = null
                    NativeAudio.previewOff(s)
                }
            },
            onChord = { keys ->
                Editing.chordCell(keys.map(::midiOf), info.mode, ctx, currentCell())?.let {
                    write(it)
                    advance()
                }
            },
            modifier = Modifier.fillMaxWidth().height(104.dp).padding(horizontal = 4.dp),
        )

        // ---- Toolbar ----
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 4.dp, vertical = 4.dp),
            horizontalArrangement = Arrangement.spacedBy(4.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            ToolButton("−", onClick = { octave = (octave - 1).coerceAtLeast(0) })
            Text("Oct $octave", style = MONO.copy(fontSize = 13.sp))
            ToolButton("+", onClick = { octave = (octave + 1).coerceAtMost(8) })
            Box(Modifier.width(4.dp))
            ToolButton("−", onClick = { step = (step - 1).coerceAtLeast(0) })
            Text("Step $step", style = MONO.copy(fontSize = 13.sp))
            ToolButton("+", onClick = { step = (step + 1).coerceAtMost(16) })
            Box(Modifier.weight(1f))
            ToolButton("Off", onClick = {
                write(currentCell().copy(note = CellData.NOTE_OFF, accidental = 0))
                advance()
            })
            ToolButton("Del", onClick = {
                write(Editing.clear(currentCell(), field))
                anchor = null
                advance()
            })
            ToolButton("Follow", selected = follow, onClick = { follow = !follow })
        }
    }
}

/**
 * One heading per column, lined up over the cells, naming the instrument the
 * column plays. Tapping a heading selects the column and offers the song's
 * instruments for it, and column actions. A "+" after the last adds a column.
 * Columns belong to the channel, so adding or removing one changes all of
 * the channel's patterns.
 */
@Composable
private fun ColumnHeadings(
    info: PatternInfo,
    channelName: String,
    selectedColumn: Int,
    columnWidth: Dp,
    onSelect: (Int) -> Unit,
    onClear: (Int) -> Unit,
    onAdd: () -> Unit,
    onRemove: (Int) -> Unit,
) {
    val colors = MaterialTheme.colorScheme
    val names = InstrumentStore.names
    InstrumentStore.assignments  // read, so a new assignment redraws the headings
    Row(Modifier.fillMaxWidth().padding(horizontal = 6.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.width(30.dp))
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

/** Columns a channel can have (the engine's kMaxColumns). */
private const val MAX_COLUMNS = 4

/** Speeds that divide a beat evenly, with what they're called. */
private val SPEED_PRESETS = listOf(3 to "8/beat", 6 to "4/beat", 8 to "3/beat", 12 to "2/beat", 24 to "1/beat")

/**
 * Edits a pattern's length (rows) and speed (ticks per row). Returns the new
 * (rows, ticksPerRow), or null if cancelled.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ShapeDialog(info: PatternInfo, onDone: (Pair<Int, Int>?) -> Unit) {
    var rowsText by remember { mutableStateOf(info.rows.toString()) }
    var speedText by remember { mutableStateOf(info.ticksPerRow.toString()) }
    val rows = rowsText.toIntOrNull()?.takeIf { it in 1..256 }
    val speed = speedText.toIntOrNull()?.takeIf { it in 1..255 }

    AlertDialog(
        onDismissRequest = { onDone(null) },
        title = { Text("Length and speed") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Stepper("Rows (1–256)", rowsText, { rowsText = it }, rows, 1..256,
                    listOf("−" to { v -> v - 1 }, "+" to { v -> v + 1 }, "÷2" to { v -> v / 2 }, "×2" to { v -> v * 2 }))
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
                if (rows != null && rows < info.rows) {
                    Text(
                        "Rows past the new end are hidden, not deleted, while the app stays open: make the pattern longer again to bring them back. Saved songs leave them out.",
                        style = MaterialTheme.typography.bodySmall,
                    )
                }
            }
        },
        confirmButton = {
            TextButton(
                onClick = { if (rows != null && speed != null) onDone(rows to speed) },
                enabled = rows != null && speed != null,
            ) { Text("Set") }
        },
        dismissButton = { TextButton(onClick = { onDone(null) }) { Text("Cancel") } },
    )
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
    // are transparent except for the playing row, which tints over it.
    val background = if (isPlaying) colors.tertiary.copy(alpha = 0.35f) else Color.Transparent
    val onBeat = info.isBeat(row)
    Row(
        Modifier.fillMaxWidth().height(ROW_HEIGHT).background(background).padding(horizontal = 6.dp),
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
                    style = MONO,
                    color = if (selected) colors.onSecondary else colors.onSurface,
                    modifier = Modifier
                        .background(if (selected) colors.secondary else Color.Transparent)
                        .clickable { onTap(c, f) }
                        .padding(horizontal = 1.dp),
                )
            }
            Row(Modifier.padding(start = 6.dp).width(columnWidth)) {
                part(CellFormat.note(cell, info.mode, ctx), Field.NOTE)
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
