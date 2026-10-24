function dump(waarom) {
    var ws = workspace.windowList ? workspace.windowList() : workspace.clientList();
    print("TSVENSTER begin " + waarom + " aantal=" + ws.length);
    for (var i = 0; i < ws.length; i++) {
        var w = ws[i];
        var g = w.geometry ? (w.geometry.x + "," + w.geometry.y + " " + w.geometry.width + "x" + w.geometry.height) : "?";
        print("TSVENSTER " + (w.resourceClass || w.resourceName || "?") + " | " + (w.caption || "") + " | " + g + " | actief=" + (workspace.activeWindow === w));
    }
    print("TSVENSTER eind " + waarom);
}
dump("bij-laden");
workspace.windowAdded.connect(function (w) { print("TSVENSTER toegevoegd " + (w.resourceClass || "?") + " | " + (w.caption || "")); });
workspace.windowActivated.connect(function (w) { print("TSVENSTER geactiveerd " + (w && w.resourceClass ? w.resourceClass : "?") + " | " + (w && w.caption ? w.caption : "")); });
