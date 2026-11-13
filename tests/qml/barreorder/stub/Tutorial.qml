pragma Singleton
import QtQuick
// Test stand-in for the guided-tour registry: TourTarget only registers and unregisters.
QtObject {
    function registerTarget(id, item) {}
    function unregisterTarget(id, item) {}
}
