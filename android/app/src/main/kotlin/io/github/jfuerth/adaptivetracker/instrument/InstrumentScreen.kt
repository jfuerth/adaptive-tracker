package io.github.jfuerth.adaptivetracker.instrument

import io.github.jfuerth.adaptivetracker.editor.PitchMode
import androidx.compose.material3.FilterChip
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
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
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.jfuerth.adaptivetracker.NativeAudio
import io.github.jfuerth.adaptivetracker.Song
import io.github.jfuerth.adaptivetracker.instrument.InstrumentParam as P
import io.github.jfuerth.adaptivetracker.ui.NameDialog
import io.github.jfuerth.adaptivetracker.ui.NoteKeyboard
import io.github.jfuerth.adaptivetracker.ui.ToolButton

/** Width of the arrows from a modulator to the stage it modulates. */
private val CONNECTOR_WIDTH = 64.dp

/** One slider step for the fine-adjust buttons, as a fraction of the slider. */
private const val FINE_STEP = 0.005f

/**
 * Instrument editor. The synth is drawn as its signal flow: oscillator and
 * noise → filter → amp down the right, with the modulators on the left, each feeding
 * the stage it modulates. Modulation amounts sit on the arrows. Tapping any
 * value opens its slider; the keyboard plays the instrument while you adjust it.
 */
/** Pitch-mode hints an instrument can carry; null: none (columns keep theirs). */
private val PITCH_HINTS = listOf(
    null to "Any",
    PitchMode.DEGREE to "Scale degrees",
    PitchMode.TRANSPOSE to "Follow key",
    PitchMode.ABSOLUTE to "Fixed",
)

@Composable
fun InstrumentScreen(modifier: Modifier = Modifier) {
    val names = InstrumentStore.names
    if (names.isEmpty()) {
        Text("This song has no instruments.", modifier.padding(16.dp))
        return
    }
    var index by rememberSaveable { mutableIntStateOf(0) }
    if (index !in names.indices) index = 0
    var selected by rememberSaveable { mutableStateOf(P.CUTOFF) }
    var octave by rememberSaveable { mutableIntStateOf(4) }
    val values = remember(index, Song.revision) { InstrumentStore.values(index) }
    if (values == null) {
        Text("This instrument could not be loaded.", modifier.padding(16.dp))
        return
    }
    val usedBy = remember(index, InstrumentStore.assignments) { InstrumentStore.usedBy(index) }
    var renaming by remember { mutableStateOf(false) }
    var creating by remember { mutableStateOf<NewInstrument?>(null) }
    var assigning by remember { mutableStateOf(false) }
    val specs = InstrumentStore.specs
    fun v(p: P) = specs[p]?.let { values.getOrNull(it.index) } ?: 0f

    // A chip showing one parameter; tapping selects it for the slider.
    // Parameters the engine doesn't have are left out.
    @Composable
    fun Chip(p: P, modifier: Modifier = Modifier, label: String = p.label) {
        if (p !in specs) return
        val colors = MaterialTheme.colorScheme
        val isSelected = p == selected
        Column(
            modifier
                .padding(2.dp)
                .clip(RoundedCornerShape(6.dp))
                .background(if (isSelected) colors.primary else colors.surfaceContainerHighest)
                .clickable { selected = p }
                .padding(horizontal = 6.dp, vertical = 3.dp),
        ) {
            Text(
                label,
                style = MaterialTheme.typography.labelSmall,
                color = if (isSelected) colors.onPrimary else colors.onSurfaceVariant,
                maxLines = 1,
            )
            Text(
                p.format(v(p)),
                fontFamily = FontFamily.Monospace,
                fontSize = 12.sp,
                color = if (isSelected) colors.onPrimary else colors.onSurface,
                maxLines = 1,
            )
        }
    }

    @Composable
    fun EnvelopeChips(a: P, d: P, s: P, r: P) {
        EnvelopeGraph(v(a), v(d), v(s), v(r), specs[a])
        Row { Chip(a, Modifier.weight(1f)); Chip(d, Modifier.weight(1f)) }
        Row { Chip(s, Modifier.weight(1f)); Chip(r, Modifier.weight(1f)) }
    }

    Column(modifier.fillMaxSize()) {
        // ---- Instrument choice ----
        Row(Modifier.fillMaxWidth().padding(horizontal = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            var menuOpen by remember { mutableStateOf(false) }
            Box {
                TextButton(onClick = { menuOpen = true }) { Text("${names[index]} ▾") }
                DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                    names.forEachIndexed { i, name ->
                        DropdownMenuItem(text = { Text(name) }, onClick = {
                            index = i
                            menuOpen = false
                        })
                    }
                    HorizontalDivider()
                    val full = InstrumentStore.isFull
                    DropdownMenuItem(text = { Text("New instrument…") }, enabled = !full, onClick = {
                        creating = NewInstrument(null)
                        menuOpen = false
                    })
                    DropdownMenuItem(text = { Text("Duplicate ${names[index]}…") }, enabled = !full, onClick = {
                        creating = NewInstrument(index)
                        menuOpen = false
                    })
                    DropdownMenuItem(text = { Text("Rename ${names[index]}…") }, onClick = {
                        renaming = true
                        menuOpen = false
                    })
                    if (full) DropdownMenuItem(
                        text = { Text("The song has ${InstrumentStore.capacity}, the most it can hold") },
                        enabled = false,
                        onClick = {},
                    )
                }
            }
            Text(
                if (usedBy.isEmpty()) "Not played · assign…" else "Plays on " + usedBy.joinToString(" · ") + " ▾",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.primary,
                maxLines = 1,
                modifier = Modifier.clip(RoundedCornerShape(4.dp)).clickable { assigning = true }.padding(4.dp),
            )
        }
        // What kind of notes this instrument plays: a column given it switches to that.
        run {
            InstrumentStore.hints  // read, so a change redraws
            val hint = InstrumentStore.pitchHint(index)
            Row(
                Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(horizontal = 12.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                Text("Notes:", style = MaterialTheme.typography.labelMedium)
                PITCH_HINTS.forEach { (mode, label) ->
                    FilterChip(
                        selected = hint == mode,
                        onClick = { InstrumentStore.setPitchHint(index, mode) },
                        label = { Text(label, style = MaterialTheme.typography.labelSmall) },
                    )
                }
            }
        }
        creating?.let { c ->
            NameDialog(
                if (c.copyOf == null) "New instrument" else "Duplicate ${names.getOrElse(c.copyOf) { "instrument" }}",
                InstrumentStore.defaultName(c.copyOf),
                "Add",
            ) { name ->
                creating = null
                if (name != null) InstrumentStore.create(c.copyOf, name)?.let { index = it }
            }
        }
        if (renaming) RenameDialog(names[index], onDone = { name ->
            if (name != null) InstrumentStore.rename(index, name)
            renaming = false
        })
        if (assigning) AssignDialog(index, onDismiss = { assigning = false })

        // ---- Signal flow ----
        Column(
            Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()).padding(horizontal = 8.dp),
        ) {
            Stage(
                modulators = listOf(
                    Modulator("Vibrato", { Chip(P.VIBRATO_RATE) }, "pitch") { Chip(P.VIBRATO_DEPTH) },
                    // The filter envelope (shown with the filter below) can also
                    // sweep the pitch: a falling sweep is what makes a kick or a tom.
                    Modulator(
                        "Filter envelope",
                        {
                            Text(
                                "The one below",
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.padding(start = 2.dp),
                            )
                        },
                        "pitch",
                    ) { Chip(P.PITCH_ENV, label = "Amount") },
                ),
                main = {
                    // Oscillator and noise are mixed ahead of the filter.
                    Column {
                        Block("Oscillator") {
                            WaveformGraph(v(P.SHAPE))
                            Chip(P.SHAPE, Modifier.fillMaxWidth())
                        }
                        Block("Noise") {
                            Chip(P.NOISE_MIX, Modifier.fillMaxWidth())
                            Chip(P.NOISE_DECAY, Modifier.fillMaxWidth())
                        }
                    }
                },
            )
            DownArrow()
            Stage(
                modulators = listOf(
                    Modulator("Filter envelope", { EnvelopeChips(P.FILTER_ATTACK, P.FILTER_DECAY, P.FILTER_SUSTAIN, P.FILTER_RELEASE) }, "cutoff") {
                        Chip(P.FILTER_ENV_AMOUNT)
                    },
                ),
                main = {
                    Block("Filter") {
                        Chip(P.FILTER_MODE, Modifier.fillMaxWidth())
                        Chip(P.CUTOFF, Modifier.fillMaxWidth())
                        Chip(P.RESONANCE, Modifier.fillMaxWidth())
                    }
                },
            )
            DownArrow()
            Stage(
                modulators = listOf(
                    Modulator("Amp envelope", { EnvelopeChips(P.AMP_ATTACK, P.AMP_DECAY, P.AMP_SUSTAIN, P.AMP_RELEASE) }, "gain", null),
                    Modulator("Tremolo", { Chip(P.TREMOLO_RATE) }, "gain") { Chip(P.TREMOLO_DEPTH) },
                ),
                main = {
                    Block("Amp") { Chip(P.LEVEL, Modifier.fillMaxWidth()) }
                },
            )
            DownArrow()
            Block("Out") { OutputMonitor(index, Modifier.fillMaxWidth()) }
        }

        // ---- Slider for the selected parameter ----
        val colors = MaterialTheme.colorScheme
        Column(Modifier.fillMaxWidth().background(colors.surfaceContainer).padding(horizontal = 12.dp, vertical = 4.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("${selected.group} · ${selected.label}", style = MaterialTheme.typography.titleSmall)
                Box(Modifier.weight(1f))
                Text(selected.format(v(selected)), fontFamily = FontFamily.Monospace, fontSize = 14.sp)
            }
            val spec = specs[selected]
            if (spec != null) Row(verticalAlignment = Alignment.CenterVertically) {
                val p = selected
                fun nudge(dir: Int) =
                    InstrumentStore.set(index, p, spec.fromPosition(spec.toPosition(v(p)) + dir * spec.nudgeStep(FINE_STEP)))
                ToolButton("◀", onClick = { nudge(-1) })
                Slider(
                    value = spec.toPosition(v(p)),
                    onValueChange = { InstrumentStore.set(index, p, spec.fromPosition(it)) },
                    steps = spec.sliderSteps,
                    modifier = Modifier.weight(1f).padding(horizontal = 6.dp),
                )
                ToolButton("▶", onClick = { nudge(1) })
            }
        }

        // ---- Keyboard: play the instrument while adjusting it ----
        val slots = remember { arrayOfNulls<Int>(4) }
        fun midiOf(key: Int) = ((octave + 1) * 12 + key).coerceIn(0, 127)
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 4.dp, vertical = 4.dp),
            horizontalArrangement = Arrangement.spacedBy(4.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            ToolButton("−", onClick = { octave = (octave - 1).coerceAtLeast(0) })
            Text("Oct $octave", fontFamily = FontFamily.Monospace, fontSize = 13.sp)
            ToolButton("+", onClick = { octave = (octave + 1).coerceAtMost(8) })
        }
        NoteKeyboard(
            firstNote = midiOf(0),
            dimNote = { false },
            onNoteDown = { n ->
                val free = slots.indexOfFirst { it == null }
                if (free >= 0) {
                    slots[free] = n
                    NativeAudio.previewOn(index, free, n.coerceIn(0, 127))
                }
            },
            onNoteUp = { k ->
                val s = slots.indexOf(k)
                if (s >= 0) {
                    slots[s] = null
                    NativeAudio.previewOff(s)
                }
            },
            onChord = {},
            modifier = Modifier.fillMaxWidth().height(104.dp).padding(horizontal = 4.dp, vertical = 2.dp),
        )
    }
}

/** A modulator box on the left, and the arrow into its stage: a label and an optional amount chip. */
private class Modulator(
    val title: String,
    val content: @Composable () -> Unit,
    val target: String,
    val amount: (@Composable () -> Unit)?,
)

/** One row of the flow: modulators (with their arrows) on the left, the stage they feed on the right. */
@Composable
private fun Stage(modulators: List<Modulator>, main: @Composable () -> Unit) {
    Row(Modifier.fillMaxWidth().height(IntrinsicSize.Min), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1.5f)) {
            modulators.forEach { m ->
                Row(Modifier.height(IntrinsicSize.Min), verticalAlignment = Alignment.CenterVertically) {
                    Block(m.title, Modifier.weight(1f)) { m.content() }
                    Connector(m.target, m.amount)
                }
            }
        }
        Box(Modifier.weight(1f).fillMaxHeight(), contentAlignment = Alignment.Center) { main() }
    }
}

/** A titled, outlined box. */
@Composable
private fun Block(title: String, modifier: Modifier = Modifier, content: @Composable () -> Unit) {
    val colors = MaterialTheme.colorScheme
    Column(
        modifier
            .padding(vertical = 3.dp)
            .border(1.dp, colors.outlineVariant, RoundedCornerShape(8.dp))
            .padding(4.dp),
    ) {
        Text(
            title,
            style = MaterialTheme.typography.labelMedium,
            color = colors.primary,
            modifier = Modifier.padding(start = 2.dp, bottom = 2.dp),
        )
        content()
    }
}

/** Arrow from a modulator to its stage, labelled with what it modulates. */
@Composable
private fun Connector(target: String, amount: (@Composable () -> Unit)?) {
    val color = MaterialTheme.colorScheme.outline
    Column(Modifier.width(CONNECTOR_WIDTH), horizontalAlignment = Alignment.CenterHorizontally) {
        amount?.invoke()
        Arrow(color, Modifier.fillMaxWidth().height(10.dp))
        Text(target, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@Composable
private fun Arrow(color: Color, modifier: Modifier) {
    Canvas(modifier) {
        val y = size.height / 2
        drawLine(color, Offset(2f, y), Offset(size.width - 2f, y), strokeWidth = 2.dp.toPx())
        val head = 5.dp.toPx()
        drawLine(color, Offset(size.width - 2f, y), Offset(size.width - 2f - head, y - head), strokeWidth = 2.dp.toPx())
        drawLine(color, Offset(size.width - 2f, y), Offset(size.width - 2f - head, y + head), strokeWidth = 2.dp.toPx())
    }
}

/** The audio path from one stage to the next, down the right-hand column. */
@Composable
private fun DownArrow(label: String? = null) {
    Row(Modifier.fillMaxWidth()) {
        Box(Modifier.weight(1.5f))
        Column(Modifier.weight(1f), horizontalAlignment = Alignment.CenterHorizontally) {
            Text("↓", color = MaterialTheme.colorScheme.outline, textAlign = TextAlign.Center)
            if (label != null) Text(label, style = MaterialTheme.typography.labelMedium)
        }
    }
}

/** One and a half cycles of the synth's own oscillator at [shape]. */
@Composable
private fun WaveformGraph(shape: Float, height: Dp = 28.dp) {
    val color = MaterialTheme.colorScheme.tertiary
    val samples = remember(shape) { NativeAudio.renderWaveform(shape, 1.5f, 96) } ?: return
    Canvas(Modifier.fillMaxWidth().height(height).padding(horizontal = 4.dp, vertical = 2.dp)) {
        val path = Path()
        samples.forEachIndexed { i, y ->
            val px = i / (samples.size - 1f) * size.width
            val py = size.height * (0.5f - 0.45f * y)
            if (i == 0) path.moveTo(px, py) else path.lineTo(px, py)
        }
        drawPath(path, color, style = Stroke(width = 1.5.dp.toPx()))
    }
}

/**
 * The synth's own envelope for a note held until it reaches sustain, then
 * released. Each stage gets a width from its real duration on the time
 * sliders' log scale ([timeSpec]), so short and long envelopes both read
 * well; the sustain hold has a fixed width.
 */
@Composable
private fun EnvelopeGraph(attack: Float, decay: Float, sustain: Float, release: Float, timeSpec: ParamSpec?) {
    val color = MaterialTheme.colorScheme.tertiary
    val trace = remember(attack, decay, sustain, release) { NativeAudio.traceEnvelope(attack, decay, sustain, release) }
    Canvas(Modifier.fillMaxWidth().height(32.dp).padding(horizontal = 4.dp, vertical = 2.dp)) {
        val segment = size.width / 4f
        fun width(seconds: Float) = when {
            seconds <= 0f -> 0f
            timeSpec == null -> segment
            else -> timeSpec.toPosition(seconds) * segment
        }
        fun y(level: Float) = size.height * (1f - level)
        val points = (trace.size - 4) / 3
        val path = Path().apply { moveTo(0f, y(0f)) }
        var x = 0f
        for (stage in 0..2) {
            if (stage == 2) {  // sustain hold before the release
                val level = trace[4 + points * 2 - 1]
                x += segment
                path.lineTo(x, y(level))
            }
            val w = width(trace[stage])
            for (k in 0 until points) {
                path.lineTo(x + w * k / (points - 1f), y(trace[4 + stage * points + k]))
            }
            x += w
        }
        drawPath(path, color, style = Stroke(width = 1.5.dp.toPx()))
    }
}

@Composable
private fun RenameDialog(current: String, onDone: (String?) -> Unit) =
    NameDialog("Rename instrument", current, "Rename", onDone = onDone)

/** An instrument about to be made: a copy of [copyOf], or the synth's defaults. */
private data class NewInstrument(val copyOf: Int?)

/**
 * Which columns play instrument [index]. Ticking a column gives it this
 * instrument; to take it away, give the column another instrument (here, or
 * from the column's heading in the pattern editor).
 */
@Composable
private fun AssignDialog(index: Int, onDismiss: () -> Unit) {
    val names = InstrumentStore.names
    val slots = remember(InstrumentStore.assignments) { InstrumentStore.slots() }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Play ${names[index]} on") },
        text = {
            LazyColumn {
                items(slots) { slot ->
                    val mine = slot.instrument == index
                    Row(
                        Modifier.fillMaxWidth().clickable(enabled = !mine) {
                            InstrumentStore.assign(slot.channel, slot.column, index)
                        },
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Checkbox(checked = mine, onCheckedChange = null, modifier = Modifier.size(40.dp))
                        Text(slot.label, Modifier.weight(1f))
                        Text(
                            names.getOrNull(slot.instrument) ?: "—",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text("Done") } },
    )
}
