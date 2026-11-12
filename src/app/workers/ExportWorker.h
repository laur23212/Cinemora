#pragma once

namespace drift::worker {

// Dafin: export rendering in its own process, so a crash or a hang in a decoder, the GPU driver
// or an encoder ends that process instead of the editor.
//
//   dafin --export-worker <job.json>
//
// The job file is one JSON object, written by the editor (AppControllerProject.cpp,
// writeExportJob): "project" (Project::toJson without transcripts), "settings" (the export
// dialog's settings map) and "output" (the file to write). The worker talks back through
// WorkerChannel (progress, heartbeat, done/error). Exit code 0 = exported, 1 = the export failed,
// 2 = bad arguments or an unreadable job.
int runExportWorker(int argc, char *argv[]);

} // namespace drift::worker
