package io.github.jfuerth.adaptivetracker.mixer

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.foundation.gestures.waitForUpOrCancellation
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshots.SnapshotStateList
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import io.github.jfuerth.adaptivetracker.NativeAudio
import io.github.jfuerth.adaptivetracker.Song
import io.github.jfuerth.adaptivetracker.editor.NaturalOrder
import io.github.jfuerth.adaptivetracker.editor.PatternStore
import io.github.jfuerth.adaptivetracker.instrument.InstrumentStore
import io.github.jfuerth.adaptivetracker.ui.NameDialog
import io.github.jfuerth.adaptivetracker.ui.ToolButton
import kotlinx.coroutines.delay

private val STRIP_WIDTH = 100.dp

/** Channels a song can have (the engine's kMaxChannels). */
private const val MAX_CHANNELS = 8

/**
 * The mixing console: a strip per channel, side by side, and the global
 * section. Each strip shows the pattern it is playing (tap it to edit), the
 * patterns it plays in the section shown, in order, then its controls:
 * restart, play/pause, mute (tap to toggle, hold for a moment's silence),
 * solo, and its fader.
 */
@Composable
fun MixerScreen(
    audioOk: Boolean,
    playing: Boolean,
    currentSection: Int,
    params: SnapshotStateList<Float>,
    layerOn: SnapshotStateList<Boolean>,
    onOpenPattern: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    var section by rememberSaveable { mutableIntStateOf(0) }
    if (section >= Song.sections.size) section = 0
    // While a section plays, the strips show it.
    LaunchedEffect(currentSection) {
        if (currentSection in Song.sections.indices) section = currentSection
    }
    if (Song.sections.isEmpty()) {
        Text("This song has no sections.", modifier.padding(16.dp))
        return
    }
    // Goes up after each edit, so the sequences are read again.
    var edits by remember { mutableIntStateOf(0) }
    val channels = Song.channels
    val sequences = remember(section, edits, channels, Song.revision) {
        channels.indices.map { NativeAudio.getSequence(section, it)?.toList() ?: emptyList() }
    }
    val timing = remember(section, edits, Song.revision) { NativeAudio.getSectionTiming(section) ?: intArrayOf(0, 4, 0) }

    // What each channel plays now, and its strip's state, read a few times a second.
    var positions by remember { mutableStateOf(List(channels.size) { intArrayOf(-1, -1) }) }
    var mixer by remember { mutableStateOf(IntArray(0)) }
    LaunchedEffect(channels.size) {
        while (true) {
            positions = List(channels.size) { NativeAudio.getPlayPosition(it) }
            mixer = NativeAudio.getMixerState()
            delay(60)
        }
    }
    val hereNow = playing && currentSection == section
    var globalOpen by rememberSaveable { mutableStateOf(true) }

    var dialog by remember { mutableStateOf<Dialog?>(null) }
    var message by remember { mutableStateOf<String?>(null) }
    fun edited() {
        edits++
        Song.structureChanged()
    }
    fun delete(kind: Int, index: Int) {
        Song.delete(kind, index)?.let { message = it }
    }
    fun seq(channel: Int, op: Int, pos: Int, pattern: Int = 0) {
        if (NativeAudio.editSequence(section, channel, op, pos, pattern)) edits++
        else message = "That change couldn't be made (a sequence holds up to 64 patterns)."
    }

    Column(modifier.fillMaxSize()) {
        if (!audioOk) Text("Could not open audio output.", color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(8.dp))

        // ---- The section shown: choose, add, rename, timing; play it ----
        Row(Modifier.fillMaxWidth().padding(horizontal = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            var open by remember { mutableStateOf(false) }
            Box(Modifier.weight(1f)) {
                TextButton(onClick = { open = true }) {
                    Text("Section: ${Song.sections[section]} ▾", maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
                DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
                    Song.sections.forEachIndexed { i, name ->
                        val bridge = Song.bridgeBeats.getOrElse(i) { 0 } > 0
                        DropdownMenuItem(
                            text = { Text((if (i == section) "✓ " else "") + name + if (bridge) " (bridge)" else "") },
                            onClick = { section = i; open = false },
                        )
                    }
                    HorizontalDivider()
                    DropdownMenuItem(text = { Text("New section…") }, onClick = { open = false; dialog = Dialog.NewSection })
                    DropdownMenuItem(text = { Text("Rename this section…") }, onClick = { open = false; dialog = Dialog.RenameSection })
                    DropdownMenuItem(text = { Text("Tempo, bar and bridge…") }, onClick = { open = false; dialog = Dialog.Timing })
                    DropdownMenuItem(text = { Text("Delete this section") }, onClick = {
                        open = false
                        delete(NativeAudio.NAMES_SECTIONS, section)
                    })
                }
            }
            ToolButton(if (playing) "Stop" else "Play", onClick = {
                if (playing) NativeAudio.stop()
                else {
                    NativeAudio.setHearAll(false)
                    NativeAudio.gotoSection(section, NativeAudio.Quantize.NOW.ordinal, NativeAudio.Entry.RESTART.ordinal, false)
                }
            })
        }
        Text(
            buildString {
                append(if (timing[0] == 0) "Keeps the tempo" else "%.1f BPM".format(timing[0] / 1000f).replace(".0 ", " "))
                append(" · ${timing[1]} beats per bar")
                if (timing[2] > 0) append(" · bridge of ${timing[2]} beats")
                if (playing && !hereNow) {
                    append(" · playing ")
                    append(if (currentSection >= 0) Song.sections.getOrElse(currentSection) { "?" } else "a pattern alone")
                }
            },
            style = MaterialTheme.typography.bodySmall,
            modifier = Modifier.padding(horizontal = 16.dp),
        )
        message?.let {
            Row(Modifier.padding(start = 16.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(it, color = MaterialTheme.colorScheme.error, modifier = Modifier.weight(1f))
                TextButton(onClick = { message = null }) { Text("OK") }
            }
        }

        Row(Modifier.weight(1f).fillMaxWidth().padding(6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            // ---- Channel strips ----
            Row(
                Modifier.weight(1f).fillMaxHeight().horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                channels.forEachIndexed { ch, name ->
                    val pos = positions.getOrElse(ch) { intArrayOf(-1, -1) }
                    val entries = sequences.getOrElse(ch) { emptyList() }
                    val flags = mixer.getOrElse(ch * 2) { 0 }
                    ChannelStrip(
                        channel = ch,
                        name = name,
                        entries = entries,
                        playingPattern = if (playing) pos[0] else -1,
                        playingRow = if (playing) pos[1] else -1,
                        highlight = hereNow,
                        flags = flags,
                        volume = mixer.getOrElse(ch * 2 + 1) { 1000 } / 1000f,
                        onOpenPattern = onOpenPattern,
                        onEdit = { op, p, pattern -> seq(ch, op, p, pattern) },
                        onMove = { p, to, pattern ->
                            Song.step("Move pattern") {
                                seq(ch, NativeAudio.SEQ_REMOVE, p)
                                seq(ch, NativeAudio.SEQ_INSERT, to, pattern)
                            }
                        },
                        onNewPattern = { p ->
                            Song.step("New pattern") {
                                val i = PatternStore.create(ch, PatternStore.freshName(name))
                                if (i < 0) message = "No room for another pattern (a song holds up to 64)."
                                else seq(ch, NativeAudio.SEQ_INSERT, p, i)
                            }
                        },
                        onDeletePattern = { pattern -> delete(NativeAudio.NAMES_PATTERNS, pattern) },
                        onRename = { dialog = Dialog.RenameChannel(ch) },
                        onDelete = { delete(NativeAudio.NAMES_CHANNELS, ch) },
                        onState = { mixer = NativeAudio.getMixerState() },
                    )
                }
                if (channels.size < MAX_CHANNELS) {
                    Box(Modifier.width(STRIP_WIDTH)) {
                        TextButton(onClick = { dialog = Dialog.NewChannel }) { Text("+ Channel") }
                    }
                }
            }

            // ---- Global section ----
            if (globalOpen) {
                GlobalStrip(
                    playing = playing,
                    currentSection = currentSection,
                    params = params,
                    layerOn = layerOn,
                    onSectionChosen = { section = it },
                    onCollapse = { globalOpen = false },
                )
            } else {
                Box(
                    Modifier
                        .width(28.dp)
                        .fillMaxHeight()
                        .clip(RoundedCornerShape(8.dp))
                        .background(MaterialTheme.colorScheme.surfaceContainerHigh)
                        .clickable { globalOpen = true },
                    contentAlignment = Alignment.Center,
                ) {
                    Text("◀\nG\nl\no\nb\na\nl", style = MaterialTheme.typography.labelMedium, textAlign = TextAlign.Center)
                }
            }
        }
    }

    when (val d = dialog) {
        null -> {}
        Dialog.NewSection -> NameDialog("New section", "Section ${Song.sections.size + 1}", "Add") { name ->
            dialog = null
            if (name != null) {
                val i = NativeAudio.addSection(name)
                if (i < 0) message = "No room for another section (a song holds up to 16)."
                else {
                    edited()
                    section = i
                }
            }
        }
        Dialog.RenameSection -> NameDialog("Rename section", Song.sections[section], "Rename") { name ->
            dialog = null
            if (name != null && NativeAudio.rename(NativeAudio.NAMES_SECTIONS, section, name)) edited()
        }
        is Dialog.RenameChannel -> NameDialog("Rename channel", channels[d.channel], "Rename") { name ->
            dialog = null
            if (name != null && NativeAudio.rename(NativeAudio.NAMES_CHANNELS, d.channel, name)) edited()
        }
        Dialog.Timing -> TimingDialog(timing) { t ->
            dialog = null
            if (t != null && NativeAudio.setSection(section, t.bpm, t.beatsPerBar, t.bridgeBeats)) edited()
        }
        Dialog.NewChannel -> NewChannelDialog("Channel ${channels.size + 1}") { c ->
            dialog = null
            if (c != null) {
                Song.step("New channel") {
                    val ch = NativeAudio.addChannel(c.name, c.mode.ordinal, c.instrument)
                    if (ch < 0) {
                        message = "No room for another channel."
                    } else {
                        Song.structureChanged()
                        InstrumentStore.columnsChanged()
                        // Something to play straight away: an empty pattern, in this section.
                        val p = PatternStore.create(ch, PatternStore.freshName(c.name))
                        if (p >= 0) NativeAudio.editSequence(section, ch, NativeAudio.SEQ_INSERT, 0, p)
                        edited()
                    }
                }
            }
        }
    }
}

private sealed interface Dialog {
    data object NewSection : Dialog
    data object RenameSection : Dialog
    data object Timing : Dialog
    data object NewChannel : Dialog
    data class RenameChannel(val channel: Int) : Dialog
}

/** One channel's strip, top to bottom: name, what it plays, its patterns, controls, fader. */
@Composable
private fun ChannelStrip(
    channel: Int,
    name: String,
    entries: List<Int>,
    playingPattern: Int,
    playingRow: Int,
    highlight: Boolean,
    flags: Int,
    volume: Float,
    onOpenPattern: (Int) -> Unit,
    onEdit: (op: Int, pos: Int, pattern: Int) -> Unit,
    onMove: (pos: Int, to: Int, pattern: Int) -> Unit,
    onNewPattern: (pos: Int) -> Unit,
    onDeletePattern: (pattern: Int) -> Unit,
    onRename: () -> Unit,
    onDelete: () -> Unit,
    onState: () -> Unit,
) {
    val colors = MaterialTheme.colorScheme
    val names = PatternStore.names
    // The channel's own patterns, by name: the only ones it can play.
    val own = remember(channel, names) { PatternStore.ofChannel(channel).sortedWith(compareBy(NaturalOrder) { names[it] }) }
    val muted = flags and NativeAudio.STRIP_MUTED != 0
    val soloed = flags and NativeAudio.STRIP_SOLOED != 0
    val paused = flags and NativeAudio.STRIP_PAUSED != 0

    Column(
        Modifier
            .width(STRIP_WIDTH)
            .fillMaxHeight()
            .clip(RoundedCornerShape(8.dp))
            .background(colors.surfaceContainer)
            .padding(4.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        var headOpen by remember { mutableStateOf(false) }
        Box {
            Text(
                "$name ▾",
                style = MaterialTheme.typography.titleSmall,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(4.dp)).clickable { headOpen = true }.padding(4.dp),
            )
            DropdownMenu(expanded = headOpen, onDismissRequest = { headOpen = false }) {
                DropdownMenuItem(text = { Text("Rename channel…") }, onClick = { headOpen = false; onRename() })
                DropdownMenuItem(text = { Text("Delete channel") }, onClick = { headOpen = false; onDelete() })
            }
        }

        // What it plays now (else what it would play first): tap to edit it.
        val shown = playingPattern.takeIf { it in own } ?: entries.firstOrNull() ?: own.firstOrNull()
        PatternThumbnail(
            pattern = shown ?: -1,
            playingRow = if (shown != null && shown == playingPattern) playingRow else -1,
            dim = paused || muted,
            modifier = Modifier.fillMaxWidth().height(84.dp),
            onClick = { if (shown != null) onOpenPattern(shown) },
        )

        // Its patterns in the section shown, in the order they play.
        Column(
            Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(3.dp),
        ) {
            if (entries.isEmpty()) {
                Text("Silent here", style = MaterialTheme.typography.bodySmall, color = colors.onSurfaceVariant)
            }
            entries.forEachIndexed { pos, pattern ->
                var open by remember { mutableStateOf(false) }
                val isPlaying = highlight && pattern == playingPattern
                Box {
                    Text(
                        names.getOrElse(pattern) { "?" },
                        style = MaterialTheme.typography.bodySmall,
                        color = if (isPlaying) colors.onPrimary else colors.onSecondaryContainer,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier
                            .fillMaxWidth()
                            .clip(RoundedCornerShape(6.dp))
                            .background(if (isPlaying) colors.primary else colors.secondaryContainer)
                            .clickable { open = true }
                            .padding(horizontal = 6.dp, vertical = 6.dp),
                    )
                    DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
                        DropdownMenuItem(text = { Text("Edit pattern") }, onClick = { open = false; onOpenPattern(pattern) })
                        DropdownMenuItem(text = { Text("Move up") }, enabled = pos > 0, onClick = {
                            open = false
                            onMove(pos, pos - 1, pattern)
                        })
                        DropdownMenuItem(text = { Text("Move down") }, enabled = pos < entries.size - 1, onClick = {
                            open = false
                            onMove(pos, pos + 1, pattern)
                        })
                        DropdownMenuItem(text = { Text("New pattern below") }, onClick = { open = false; onNewPattern(pos + 1) })
                        DropdownMenuItem(text = { Text("Remove from here") }, onClick = { open = false; onEdit(NativeAudio.SEQ_REMOVE, pos, 0) })
                        DropdownMenuItem(text = { Text("Delete pattern (everywhere)") }, onClick = { open = false; onDeletePattern(pattern) })
                        if (own.size > 1) {
                            HorizontalDivider()
                            Text("Play instead:", style = MaterialTheme.typography.labelMedium, modifier = Modifier.padding(horizontal = 12.dp))
                            own.filter { it != pattern }.forEach { p ->
                                DropdownMenuItem(text = { Text(names[p]) }, onClick = { open = false; onEdit(NativeAudio.SEQ_SET, pos, p) })
                            }
                        }
                    }
                }
            }
            var addOpen by remember { mutableStateOf(false) }
            Box {
                Text(
                    "+ Pattern",
                    style = MaterialTheme.typography.labelMedium,
                    color = colors.primary,
                    modifier = Modifier.clip(RoundedCornerShape(4.dp)).clickable { addOpen = true }.padding(6.dp),
                )
                DropdownMenu(expanded = addOpen, onDismissRequest = { addOpen = false }) {
                    DropdownMenuItem(text = { Text("New empty pattern") }, onClick = { addOpen = false; onNewPattern(entries.size) })
                    if (own.isNotEmpty()) {
                        HorizontalDivider()
                        own.forEach { p ->
                            DropdownMenuItem(text = { Text(names[p]) }, onClick = {
                                addOpen = false
                                onEdit(NativeAudio.SEQ_INSERT, entries.size, p)
                            })
                        }
                    }
                }
            }
        }

        // Controls: restart (at the next bar), play/pause, mute, solo.
        Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            StripButton("⟲", on = false, onColor = colors.primary, modifier = Modifier.weight(1f)) {
                NativeAudio.restartChannel(channel)
            }
            StripButton(if (paused) "▶" else "⏸", on = paused, onColor = colors.tertiary, modifier = Modifier.weight(1f)) {
                NativeAudio.setChannelPause(channel, !paused)
                onState()
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            MuteButton(muted, modifier = Modifier.weight(1f)) { on ->
                NativeAudio.setChannelMute(channel, on)
                onState()
            }
            StripButton("S", on = soloed, onColor = Color(0xFFE0A000), modifier = Modifier.weight(1f)) {
                NativeAudio.setChannelSolo(channel, !soloed)
                onState()
            }
        }

        // The fader.
        var dragging by remember { mutableStateOf<Float?>(null) }
        val shownVolume = dragging ?: volume
        Text("${(shownVolume * 100).toInt()}", style = MaterialTheme.typography.labelSmall)
        VerticalFader(
            value = shownVolume,
            onValue = {
                dragging = it
                NativeAudio.setChannelVolume(channel, it)
            },
            onRelease = { dragging = null },
            modifier = Modifier.width(40.dp).height(130.dp),
        )
    }
}

/** A square strip button: lit while [on]. */
@Composable
private fun StripButton(text: String, on: Boolean, onColor: Color, modifier: Modifier = Modifier, onClick: () -> Unit) {
    val colors = MaterialTheme.colorScheme
    Box(
        modifier
            .height(34.dp)
            .clip(RoundedCornerShape(6.dp))
            .background(if (on) onColor else colors.secondaryContainer)
            .clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(text, style = MaterialTheme.typography.labelLarge, color = if (on) Color.White else colors.onSecondaryContainer)
    }
}

/**
 * Mute: a tap toggles it; holding it flips it only while held (a moment's
 * silence, or a moment's sound if it was muted).
 */
@Composable
private fun MuteButton(muted: Boolean, modifier: Modifier = Modifier, onSet: (Boolean) -> Unit) {
    val colors = MaterialTheme.colorScheme
    val current by rememberUpdatedState(muted)
    val set by rememberUpdatedState(onSet)
    var pressed by remember { mutableStateOf<Boolean?>(null) }  // what it shows while held
    val shown = pressed ?: muted
    Box(
        modifier
            .height(34.dp)
            .clip(RoundedCornerShape(6.dp))
            .background(if (shown) colors.error else colors.secondaryContainer)
            .pointerInput(Unit) {
                awaitEachGesture {
                    val down = awaitFirstDown()
                    val was = current
                    pressed = !was
                    set(!was)
                    val up = waitForUpOrCancellation()
                    val held = (up?.uptimeMillis ?: down.uptimeMillis) - down.uptimeMillis > HOLD_MS || up == null
                    if (held) set(was)  // momentary: back as it was
                    pressed = null
                }
            },
        contentAlignment = Alignment.Center,
    ) {
        Text("M", style = MaterialTheme.typography.labelLarge, color = if (shown) colors.onError else colors.onSecondaryContainer)
    }
}

private const val HOLD_MS = 350L

/**
 * A vertical fader, 0 at the bottom to 1 at the top, moved by dragging (not
 * by tapping, so a touch doesn't jump it).
 */
@Composable
internal fun VerticalFader(
    value: Float,
    onValue: (Float) -> Unit,
    modifier: Modifier = Modifier,
    onRelease: () -> Unit = {},
) {
    val colors = MaterialTheme.colorScheme
    val current by rememberUpdatedState(value)
    val set by rememberUpdatedState(onValue)
    val release by rememberUpdatedState(onRelease)
    var heightPx by remember { mutableFloatStateOf(1f) }
    Canvas(
        modifier
            .onSizeChanged { heightPx = it.height.toFloat().coerceAtLeast(1f) }
            .pointerInput(Unit) {
                var v = 0f
                detectVerticalDragGestures(
                    onDragStart = { v = current },
                    onDragEnd = { release() },
                    onDragCancel = { release() },
                    onVerticalDrag = { change, dy ->
                        change.consume()
                        v = (v - dy / (heightPx * 0.85f)).coerceIn(0f, 1f)
                        set(v)
                    },
                )
            },
    ) {
        val knob = 18.dp.toPx()
        val track = 4.dp.toPx()
        val x = size.width / 2
        val top = knob / 2
        val bottom = size.height - knob / 2
        val y = top + (1f - value.coerceIn(0f, 1f)) * (bottom - top)
        drawRoundRect(colors.outlineVariant, Offset(x - track / 2, top), Size(track, bottom - top), CornerRadius(track / 2))
        drawRoundRect(colors.primary, Offset(x - track / 2, y), Size(track, bottom - y), CornerRadius(track / 2))
        drawRoundRect(colors.primary, Offset(0f, y - knob / 2), Size(size.width, knob), CornerRadius(4.dp.toPx()))
        drawLine(colors.onPrimary, Offset(4.dp.toPx(), y), Offset(size.width - 4.dp.toPx(), y), 2.dp.toPx())
    }
}

/**
 * A pattern drawn small: a lane per column with its notes placed by pitch,
 * faint lines on the beats, and the row playing now.
 */
@Composable
private fun PatternThumbnail(pattern: Int, playingRow: Int, dim: Boolean, modifier: Modifier, onClick: () -> Unit) {
    val colors = MaterialTheme.colorScheme
    val loaded = remember(pattern, PatternStore.shapes, Song.revision) { if (pattern >= 0) PatternStore.get(pattern) else null }
    Canvas(
        modifier
            .clip(RoundedCornerShape(6.dp))
            .background(colors.surfaceContainerLowest)
            .clickable(onClick = onClick),
    ) {
        val p = loaded ?: return@Canvas
        val info = p.info
        val rows = info.rows.coerceAtLeast(1)
        val cols = info.columns.coerceAtLeast(1)
        val rowH = size.height / rows
        val laneW = size.width / cols
        val line = 1.dp.toPx()
        for (r in 0 until rows) {
            if (info.isBeat(r)) drawRect(colors.outlineVariant.copy(alpha = 0.5f), Offset(0f, r * rowH), Size(size.width, line))
        }
        val noteColor = if (dim) colors.onSurfaceVariant.copy(alpha = 0.4f) else colors.primary
        val dash = (laneW / 4).coerceAtLeast(3.dp.toPx())
        val noteH = rowH.coerceAtLeast(2.dp.toPx())
        for (c in 0 until cols) {
            var lo = 127
            var hi = 0
            for (r in 0 until rows) {
                val n = p.cell(r, c).note
                if (n in 0..127) {
                    if (n < lo) lo = n
                    if (n > hi) hi = n
                }
            }
            val span = (hi - lo).coerceAtLeast(1)
            for (r in 0 until rows) {
                val cell = p.cell(r, c)
                if (!cell.isNote) continue
                val x = c * laneW + 2.dp.toPx() + (cell.note - lo).toFloat() / span * (laneW - dash - 4.dp.toPx())
                drawRect(noteColor, Offset(x, r * rowH), Size(dash, noteH))
            }
        }
        if (playingRow in 0 until rows) {
            drawRect(colors.tertiary.copy(alpha = 0.45f), Offset(0f, playingRow * rowH), Size(size.width, noteH))
        }
    }
}
