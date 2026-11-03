// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import "bootstrap" as Native

Item {
    Native.FluidController {
        pluginRoot: Qt.resolvedUrl("../native")
    }
}
