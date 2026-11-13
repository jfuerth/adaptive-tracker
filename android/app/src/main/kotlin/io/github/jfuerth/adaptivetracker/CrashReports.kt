package io.github.jfuerth.adaptivetracker

import android.app.ActivityManager
import android.app.ApplicationExitInfo
import android.content.Context
import android.os.Build
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Keeps a report of the last crash so it can be shared from the app after a
 * restart, since there is no adb connection to read logs from.
 *
 * - Kotlin/Java crashes: an uncaught-exception handler writes the stack trace
 *   to a file before the process dies.
 * - Native crashes and freezes (Android 11+): the system records how the
 *   process ended; the next launch reads that record, including the native
 *   tombstone's readable text (library and function names in the backtrace).
 */
object CrashReports {
    private const val FILE = "last-crash.txt"
    private const val PREFS = "crash-reports"
    private const val SEEN_EXIT = "seen-exit-timestamp"
    private const val MAX_TRACE_CHARS = 24_000

    /** Call once, early in onCreate. */
    fun install(context: Context) {
        val app = context.applicationContext
        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, e ->
            runCatching {
                val trace = StringWriter().also { e.printStackTrace(PrintWriter(it)) }.toString()
                File(app.filesDir, FILE).writeText(
                    header(app) + "Uncaught exception on thread \"${thread.name}\"\n\n" + trace,
                )
            }
            previous?.uncaughtException(thread, e)
        }
    }

    /** A report of a crash since the last dismiss, or null. */
    fun pending(context: Context): String? {
        val file = File(context.filesDir, FILE)
        if (file.exists()) return runCatching { file.readText() }.getOrNull()
        return lastAbnormalExit(context)
    }

    /** Forget the current report. */
    fun dismiss(context: Context) {
        File(context.filesDir, FILE).delete()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val latest = exits(context).maxOfOrNull { it.timestamp } ?: return
            context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putLong(SEEN_EXIT, latest).apply()
        }
    }

    private fun exits(context: Context): List<ApplicationExitInfo> {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return emptyList()
        val am = context.getSystemService(ActivityManager::class.java) ?: return emptyList()
        return runCatching { am.getHistoricalProcessExitReasons(context.packageName, 0, 5) }.getOrDefault(emptyList())
    }

    private fun lastAbnormalExit(context: Context): String? {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return null
        val seen = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getLong(SEEN_EXIT, 0L)
        val exit = exits(context).firstOrNull {
            it.timestamp > seen && it.reason in setOf(
                ApplicationExitInfo.REASON_CRASH,
                ApplicationExitInfo.REASON_CRASH_NATIVE,
                ApplicationExitInfo.REASON_ANR,
            )
        } ?: return null

        val kind = when (exit.reason) {
            ApplicationExitInfo.REASON_CRASH_NATIVE -> "Native crash"
            ApplicationExitInfo.REASON_ANR -> "App not responding"
            else -> "Crash"
        }
        val trace = runCatching {
            exit.traceInputStream?.use { it.readBytes() }
        }.getOrNull()?.let { bytes ->
            // Native crashes give a binary tombstone; keep its readable strings.
            if (exit.reason == ApplicationExitInfo.REASON_CRASH_NATIVE) readableStrings(bytes) else String(bytes)
        }
        return header(context) +
            "$kind at ${format(exit.timestamp)}\n" +
            "Description: ${exit.description ?: "-"}\n" +
            "Signal/status: ${exit.status}\n\n" +
            (trace?.take(MAX_TRACE_CHARS) ?: "(no trace recorded)")
    }

    /** Runs of printable ASCII of 4+ characters, one per line, like `strings`. */
    private fun readableStrings(bytes: ByteArray): String {
        val out = StringBuilder()
        val run = StringBuilder()
        fun flush() {
            if (run.length >= 4) out.append(run).append('\n')
            run.setLength(0)
        }
        for (b in bytes) {
            val c = b.toInt() and 0xFF
            if (c in 0x20..0x7E) run.append(c.toChar()) else flush()
        }
        flush()
        return out.toString()
    }

    private fun header(context: Context): String {
        val version = runCatching {
            context.packageManager.getPackageInfo(context.packageName, 0).versionName
        }.getOrNull() ?: "?"
        return "Adaptive Tracker $version · Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT}) · " +
            "${Build.MANUFACTURER} ${Build.MODEL}\nReported ${format(System.currentTimeMillis())}\n\n"
    }

    private fun format(millis: Long) = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date(millis))
}
