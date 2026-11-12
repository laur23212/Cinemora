#pragma once

class AppController;
class ExportQueue;

// Dafin: connects the export queue to the editor and the system — snapshots the open project
// (after template titles' final renders), renders with Exporter, waits while a normal export
// runs, and shows an OS notification when a job ends. Called once from main.cpp.
void installExportQueue(ExportQueue &queue, AppController &app);
