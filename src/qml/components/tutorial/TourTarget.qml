import QtQuick
import Drift

// Gives a UI element a stable tour id (for example "bar.export") so the guided tour can find and
// highlight it. It draws nothing and changes nothing about the element. An empty tourId
// registers nothing, and the element is unregistered when it is destroyed.
//
//     TourTarget { tourId: "bar.save"; target: saveButton }
QtObject {
    id: root

    property string tourId: ""
    property Item target: null

    property string _id: ""
    property Item _item: null

    function _sync() {
        if (root._id !== "" && root._item)
            Tutorial.unregisterTarget(root._id, root._item)
        root._id = root.tourId
        root._item = root.target
        if (root._id !== "" && root._item)
            Tutorial.registerTarget(root._id, root._item)
    }

    onTourIdChanged: _sync()
    onTargetChanged: _sync()
    Component.onCompleted: _sync()
    Component.onDestruction: {
        if (root._id !== "" && root._item)
            Tutorial.unregisterTarget(root._id, root._item)
    }
}
