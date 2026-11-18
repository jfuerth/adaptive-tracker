package io.github.jfuerth.adaptivetracker.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import io.github.jfuerth.adaptivetracker.ui.ToolButton
import kotlin.math.roundToInt

/**
 * The effect reference: pick an effect from the list, then set its values.
 * Opens on the cell's own effect if it has one. [onDone] gets the changed
 * cell, or null if cancelled.
 */
@Composable
fun EffectDialog(
    cell: CellData,
    mode: PitchMode,
    eventNames: List<String>,
    effectValue: (effect: Int, param: Int) -> Float,
    ticksPerBeat: Int,
    onDone: (CellData?) -> Unit,
) {
    var kind by remember { mutableStateOf(EffectKind.of(cell)) }
    var choosing by remember { mutableStateOf(kind == null) }
    val values = remember { mutableStateListOf<Int>().apply { kind?.let { addAll(it.valuesOf(cell.param)) } } }

    fun choose(k: EffectKind) {
        // Keep the values when coming back to the cell's own effect.
        val start = if (k == EffectKind.of(cell)) k.valuesOf(cell.param) else k.values.map { 0 }
        values.clear()
        values.addAll(start)
        kind = k
        choosing = false
    }
    // Events: the slider covers the song's events, not all 256.
    fun maxOf(k: EffectKind, i: Int): Int =
        if (k.command == 'B') (eventNames.size - 1).coerceIn(0, 0xFF) else k.values[i].max

    val k = kind
    AlertDialog(
        onDismissRequest = { onDone(null) },
        title = { Text(if (choosing || k == null) "Choose an effect" else "${k.name} · ${k.code}") },
        text = {
            if (choosing || k == null) {
                EffectList(current = k, onChoose = ::choose)
            } else {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    k.values.forEachIndexed { i, spec ->
                        val max = maxOf(k, i)
                        val v = values.getOrElse(i) { 0 }.coerceIn(0, max)
                        fun set(n: Int) { values[i] = n.coerceIn(0, max) }
                        Text(spec.label, style = MaterialTheme.typography.labelLarge)
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            ToolButton("−", horizontalPadding = 8.dp, onClick = { set(v - 1) })
                            Slider(
                                value = v.toFloat(),
                                onValueChange = { set(it.roundToInt()) },
                                valueRange = 0f..max.coerceAtLeast(1).toFloat(),
                                steps = (max - 1).coerceAtLeast(0),
                                enabled = max > 0,
                                modifier = Modifier.weight(1f).padding(horizontal = 4.dp),
                            )
                            ToolButton("+", horizontalPadding = 8.dp, onClick = { set(v + 1) })
                            Text(
                                if (spec.max > 0xF) "%02X".format(v) else "%X".format(v),
                                fontFamily = FontFamily.Monospace,
                                modifier = Modifier.padding(start = 8.dp).width(24.dp),
                            )
                        }
                    }
                    Text(
                        EffectDescription.describe(k.apply(cell, values), mode, eventNames, effectValue, ticksPerBeat),
                        style = MaterialTheme.typography.bodyMedium,
                    )
                }
            }
        },
        confirmButton = {
            if (!choosing && k != null) TextButton(onClick = { onDone(k.apply(cell, values)) }) { Text("Set") }
        },
        dismissButton = {
            Row {
                if (!choosing && k != null) TextButton(onClick = { choosing = true }) { Text("Other effect") }
                if (cell.effect != CellData.EFFECT_NONE || cell.param != 0) {
                    TextButton(onClick = { onDone(cell.copy(effect = CellData.EFFECT_NONE, param = 0)) }) { Text("Clear") }
                }
                TextButton(onClick = { onDone(null) }) { Text("Cancel") }
            }
        },
    )
}

/** Every effect, under group headings; [current] is highlighted. */
@Composable
private fun EffectList(current: EffectKind?, onChoose: (EffectKind) -> Unit) {
    val colors = MaterialTheme.colorScheme
    val rows = remember {
        EffectKind.all.groupBy { it.group }.flatMap { (group, kinds) -> listOf<Any>(group) + kinds }
    }
    LazyColumn(Modifier.heightIn(max = 420.dp)) {
        items(rows) { row ->
            when (row) {
                is String -> Text(
                    row,
                    style = MaterialTheme.typography.labelLarge,
                    color = colors.primary,
                    modifier = Modifier.padding(top = 12.dp, bottom = 4.dp),
                )
                is EffectKind -> Row(
                    Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(6.dp))
                        .background(if (row == current) colors.secondaryContainer else colors.surface.copy(alpha = 0f))
                        .clickable { onChoose(row) }
                        .padding(vertical = 8.dp, horizontal = 4.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(row.code, fontFamily = FontFamily.Monospace, modifier = Modifier.width(36.dp))
                    Text(row.name)
                }
            }
        }
    }
}
