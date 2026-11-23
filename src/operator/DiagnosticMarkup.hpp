#pragma once

namespace vosc::operator_ui {
inline constexpr char documentMarkup[] = R"RML(
<rml>
<head>
<style>
body {
    display: block; position: absolute; width: 100%; height: 100%; margin: 0;
    color: #f3f6fc; font-family: LatoLatin; font-size: 14dp;
}
#dock {
    position: absolute; left: 0; right: 0; bottom: 30dp; margin: 0 auto;
    width: 364dp; max-width: 95%; height: 50dp; box-sizing: border-box; padding: 5dp;
    background-color: #1b2534d4; border: 1dp #e7f1ff73; border-radius: 25dp;
    display: flex; align-items: stretch;
}
#panel {
    position: absolute; left: 0; right: 0; bottom: 90dp; margin: 0 auto;
    display: flex; flex-direction: column; overflow: hidden;
    width: 500dp; max-width: 95%; height: 210dp;
    box-sizing: border-box;
    background-color: #121721f2; border: 1dp #d5e6ff4d; border-radius: 16dp;
}
#panel.console { height: 468dp; }
#panel.overview.quiet { height: 170dp; }
#panel.overview.camera { height: 244dp; }
#panel.overview.quiet.camera { height: 204dp; }
#camera-reading { display: none; }
#panel.overview.camera #camera-reading {
    display: block; flex: none; margin: 0 12dp; padding-bottom: 5dp; border-bottom: 1dp #d2e1fc29;
}
#camera-reading .readout-label { display: inline; margin-right: 6dp; }
#panel.enlarged { width: 620dp; }
body.compact #dock { width: 341dp; height: 43dp; bottom: 12dp; padding: 4dp; border-radius: 21.5dp; }
body.compact #panel { width: 390dp; bottom: 61dp; }
body.compact #panel.console { height: 264dp; }
#panel-header {
    display: flex; flex: none; align-items: center; box-sizing: border-box;
    height: 45dp; padding: 0 10dp; border-bottom: 1dp #d2e1fc29;
}
#panel-title {
    flex: 1; min-width: 0; overflow: hidden; white-space: nowrap;
    font-size: 14dp; font-weight: bold;
}
button {
    display: inline-block; tab-index: auto; margin: 0 6dp 5dp 0; font-size: 13dp;
    padding: 6dp 9dp; border: 1dp #b9d2f54a; border-radius: 8dp;
    background-color: #b9d2f511; white-space: nowrap;
}
#panel-header button { flex: none; margin: 0; padding: 6dp 7dp; font-size: 12dp; }
#panel-header #back { display: none; margin-right: 8dp; }
#panel.detail #back { display: block; }
#return { background-color: transparent; border-color: transparent; }
#return span { padding: 1dp 4dp; margin-left: 4dp; border: 1dp #b4c4dc52; border-radius: 4dp; font-size: 11dp; }
#dock button {
    flex: 1; min-width: 0; display: block; margin: 0; padding: 0;
    text-align: center; line-height: 36dp; font-size: 13dp; border-radius: 19dp;
    color: #e6eefb;
    border-color: transparent; background-color: transparent;
}
body.compact #dock button { line-height: 31dp; border-radius: 16.5dp; }
button.selected, #dock button.selected {
    color: #fff; background-color: #b6d4fa2e; border-color: #d3e8ff45;
}
#dock button.selected { font-weight: bold; }
button:hover, #dock button:hover, #return:hover { background-color: #b6d4fa29; }
button:focus-visible, #dock button:focus-visible, #return:focus-visible {
    border-color: #a9dcff; background-color: #b6d4fa32;
}
#readings {
    display: none; flex: none; align-items: flex-start;
    margin: 0 12dp; padding: 12dp 0 9dp; border-bottom: 1dp #d2e1fc29;
}
#panel.overview #readings { display: flex; }
#scene-title { flex: 1; min-width: 0; font-size: 16dp; font-weight: bold; word-break: break-word; }
.readout { display: block; width: 108dp; margin-left: 12dp; font-size: 12dp; }
.readout-label { display: block; color: #bfc9d9; font-size: 11dp; margin: 3dp 0 2dp; }
#content, #log-rows {
    display: block; flex: 1; min-height: 0; overflow-y: auto; padding: 9dp 12dp 10dp;
}
#console-controls, #console-status, #log-rows { display: none; }
#panel.console #content { display: none; }
#panel.console #console-controls { display: block; flex: none; padding: 6dp 12dp; border-bottom: 1dp #d2e1fc29; }
#console-status.present {
    display: block; flex: none; padding: 4dp 12dp; color: #c5d4e5; font-size: 11dp;
    border-bottom: 1dp #d2e1fc29;
}
.console-toolbar { display: flex; align-items: center; }
.console-toolbar + .console-toolbar { margin-top: 5dp; }
.console-toolbar button, #console-search {
    box-sizing: border-box; height: 30dp; font-size: 12dp; line-height: 16dp;
    margin: 0 0 0 6dp; padding: 6dp 8dp;
}
.console-toolbar button:first-child { margin-left: 0; }
.console-toolbar button.inactive {
    color: #aab5c5; background-color: transparent; focus: none; pointer-events: none;
}
#console-search {
    flex: 1; min-width: 0; margin-left: 0; border: 1dp #b9d2f54a; border-radius: 6dp;
    background-color: #182130; color: #f3f6fc;
}
#console-search:focus { border-color: #a9dcff; }
#console-search:placeholder-shown { color: #c5d4e5; }
#follow { flex: none; min-width: 86dp; margin-right: -5dp; padding: 6dp 2dp; background-color: transparent; border-color: transparent; }
#follow:focus-visible { border-color: #a9dcff; }
#panel.console #log-rows { display: block; padding-top: 0; }
body.short #readings { padding: 4dp 0; }
.caption { display: block; margin-bottom: 5dp; color: #c5d4e5; }
.error-summary { color: #ffc2b3; font-weight: bold; margin-bottom: 3dp; }
.error { color: #ffd0c3; }
.message { display: block; white-space: pre-wrap; word-break: break-word; margin-bottom: 5dp; }
.message.error { background-color: #ffc2b312; border-radius: 8dp; padding: 6dp 8dp; }
#panel.inspecting #content { padding-top: 7dp; }
#panel.inspecting .caption { word-break: break-word; }
#inspector-tools { display: none; flex: none; padding: 6dp 12dp; border-bottom: 1dp #d2e1fc29; }
#inspector-tools.present { display: block; }
#inspector-tools button { font-size: 12dp; margin-bottom: 0; }
.inspect-label { display: block; margin: 4dp 0 5dp; font-size: 17dp; font-weight: bold; word-break: break-word; }
.inspect-heading { display: block; margin: 8dp 0 4dp; font-size: 12dp; font-weight: bold; color: #c5d4e5; }
button.inspect-row {
    display: flex; box-sizing: border-box; width: 100%; margin: 0 0 2dp; padding: 5dp 6dp;
    justify-content: space-between; align-items: center; text-align: left;
    background-color: transparent; border: 1dp transparent; border-bottom-color: #d2e1fc29;
    border-radius: 5dp; white-space: normal;
}
button.inspect-row span { focus: none; pointer-events: none; }
button.inspect-row:hover { background-color: #b6d4fa29; }
button.inspect-row:focus-visible { border-color: #a9dcff; background-color: #b6d4fa32; }
.inspect-row-label { display: block; flex: 1; min-width: 0; word-break: break-word; font-weight: bold; }
.inspect-row-label span { display: block; }
.inspect-row-label .inspect-subtitle, .inspect-kind { color: #c5d4e5; font-size: 12dp; font-weight: normal; }
.inspect-kind { flex: none; margin-left: 8dp; }
.inspect-layer-columns { display: flex; }
.inspect-layer-body { flex: 1; min-width: 0; }
.inspect-rail { flex: none; width: 76dp; margin-right: 7dp; border-right: 1dp #d2e1fc29; padding-right: 5dp; }
button.inspect-rail-row { padding: 4dp; font-size: 12dp; }
.inspect-rail-row .inspect-kind { display: none; }
body.compact .inspect-rail { display: none; }
span.inspect-thumb {
    display: block; flex: none; width: 39dp; height: 29dp; margin-right: 8dp;
    border-radius: 3dp; background-color: #0b1018;
}
.inspect-figure {
    display: block; height: 139dp; margin: 7dp 0; background-color: #0b1018;
}
#panel.enlarged .inspect-figure { height: 292dp; }
body.compact .inspect-figure { height: 109dp; }
body.compact #panel.enlarged .inspect-figure { height: 222dp; }
.inspect-disclosure { display: block; margin: 5dp 0; border-top: 1dp #d2e1fc29; padding-top: 5dp; }
.inspect-disclosure button { margin-bottom: 0; }
.inspect-value-row {
    display: flex; align-items: flex-start; min-height: 23dp; padding: 4dp 3dp;
    font-size: 12dp; border-bottom: 1dp #d2e1fc29; word-break: break-word;
}
.inspect-value-name { display: block; flex: 0 0 33%; padding-right: 5dp; }
.inspect-value-type { display: block; flex: 0 0 23%; color: #c5d4e5; padding-right: 5dp; }
.inspect-value-row .inspect-reading { display: block; flex: 1; min-width: 0; }
button.log-row {
    display: flex; align-items: flex-start; box-sizing: border-box; width: 100%;
    margin: 0; padding: 5dp 7dp; text-align: left; white-space: normal;
    border: 1dp transparent; border-bottom-color: #d2e1fc29; border-radius: 5dp;
    background-color: transparent;
}
button.log-row:hover { background-color: #b6d4fa29; }
button.log-row.selected { background-color: #304054; }
button.log-row:focus-visible { border-color: #a9dcff; }
button.log-row span { focus: none; }
.log-clock, .log-severity, .log-text { display: block; }
.log-clock { flex: none; width: 68dp; color: #bfc9d9; font-size: 11dp; }
.log-clock span, .log-message, .log-module { display: block; }
.log-severity { flex: none; width: 48dp; font-size: 12dp; }
.log-text { flex: 1; min-width: 0; }
.log-message { white-space: pre-wrap; word-break: break-word; }
.log-module { color: #bfc9d9; font-size: 12dp; }
scrollbarvertical { width: 8dp; background-color: #182334; }
scrollbarvertical slidertrack { background-color: #182334; }
scrollbarvertical sliderbar { min-height: 18dp; background-color: #566c88; border-radius: 4dp; }
scrollbarvertical sliderarrowdec, scrollbarvertical sliderarrowinc { display: none; }

#browse-tools { display: none; flex: none; padding: 6dp 12dp; border-bottom: 1dp #d2e1fc29; }
#panel.browse #browse-tools { display: block; }
#browse-header, #browse-filters, .browse-choice, .browse-facts { display: flex; align-items: center; }
#browse-header { justify-content: space-between; height: 25dp; }
#browse-heading { font-size: 13dp; font-weight: bold; }
#browse-summary, .browse-origin, .browse-requirements, .browse-feedback {
    color: #c5d4e5; font-size: 12dp;
}
#browse-filters { margin-top: 5dp; }
#panel.detail #browse-filters { display: none; }
#browse-search, #panel.browse select {
    box-sizing: border-box; height: 30dp; padding: 5dp 7dp; font-size: 13dp;
    border: 1dp #b9d2f54a; border-radius: 5dp; background-color: #182130; color: #f3f6fc;
}
#browse-search { flex: 1; min-width: 0; margin-right: 6dp; }
#browse-search:focus, #panel.browse select:focus { border-color: #a9dcff; }
#browse-search:placeholder-shown { color: #c5d4e5; }
#browse-category { flex: none; width: 129dp; }
#panel.browse select selectarrow {
    width: 14dp; height: 28dp; background-color: transparent; decorator: none;
}
#panel.browse select selectvalue { margin: 0; padding: 0 12dp 0 0; }
#panel.browse select selectbox {
    background-color: #1b2534; border: 1dp #b9d2f56b; border-radius: 4dp;
    max-height: 152dp; overflow-y: auto;
}
#panel.browse select option { padding: 5dp 8dp; color: #f3f6fc; }
#panel.browse select option:hover, #panel.browse select option:checked { background-color: #304054; }
#panel.browse select:disabled { color: #bfc9d9; }
#panel.browse #content { padding-top: 5dp; }
button.asset-row { min-height: 43dp; padding: 5dp 7dp; }
button.asset-row.selected { background-color: #304054; }
.asset-row .browse-chevron { flex: none; width: 12dp; height: 12dp; margin-left: 7dp; pointer-events: none; }
.browse-facts { align-items: flex-start; }
.browse-fact { flex: 1; min-width: 0; margin-right: 5dp; word-break: break-word; font-size: 13dp; }
.browse-origin { margin-top: 5dp; }
.browse-requirements, .browse-command {
    display: block; margin-top: 7dp; padding-top: 7dp; border-top: 1dp #d2e1fc29; word-break: break-word;
}
.browse-preview { display: block; margin: 7dp 0; }
.browse-preview img { height: 78dp; max-width: 100%; }
.browse-choice { margin-top: 5dp; }
.browse-field { display: flex; flex: 1; min-width: 0; align-items: center; margin-right: 8dp; }
.browse-field span { flex: none; margin: 0 5dp 0 0; font-size: 12dp; }
.browse-field select { flex: 1; min-width: 0; }
body.compact .browse-field { display: block; }
body.compact .browse-field span { display: block; margin-bottom: 2dp; }
body.compact .browse-field select { display: block; width: 100%; }
body.compact .browse-preview img { height: 60dp; }
.browse-snippet {
    display: block; margin: 4dp 0 7dp; padding: 6dp; border-radius: 4dp; background-color: #0c121c;
    font-size: 13dp; white-space: pre-wrap; word-break: break-word;
}
.browse-feedback { display: inline; }
</style>
</head>
<body>
<div id="dock">
    <button id="overview">Overview</button><button id="inspector">Inspector</button>
    <button id="console">Console</button><button id="browse">Browse</button>
</div>
<div id="panel">
    <div id="panel-header">
        <button id="back">Back</button>
        <div id="panel-title"></div>
        <button id="return">Return to output <span>Esc</span></button>
    </div>
    <div id="readings">
        <div id="scene-title"></div>
        <div class="readout"><span class="readout-label">Render size</span><span id="render-size"></span></div>
        <div class="readout"><span class="readout-label">Observed FPS</span><span id="observed-fps"></span></div>
    </div>
    <div id="camera-reading"><span class="readout-label">Camera position</span><span id="camera-position"></span></div>
    <div id="inspector-tools"></div>
    <div id="browse-tools">
        <div id="browse-header"><span id="browse-heading">Assets</span><span id="browse-summary"></span></div>
        <div id="browse-filters">
            <input id="browse-search" type="text" placeholder="Search by name" aria-label="Search assets by name" />
            <select id="browse-category" aria-label="Asset category">
                <option value="" selected>All categories</option>
                <option value="Fragment shader">Fragment shader</option>
                <option value="Image">Image</option>
                <option value="Model">Model</option>
                <option value="Built-in">Built-in</option>
            </select>
        </div>
    </div>
    <div id="content"></div>
    <div id="console-controls">
        <div class="console-toolbar">
            <input type="text" id="console-search" placeholder="Filter messages" aria-label="Filter messages" />
            <button id="errors-only">Errors only</button>
            <button id="follow">Following latest</button>
        </div>
        <div class="console-toolbar">
            <button id="jump-latest">Jump to latest</button>
            <button id="copy-filtered">Copy filtered log</button>
            <button id="clear">Clear view</button>
            <button id="copy">Copy entry</button>
        </div>
    </div>
    <div id="console-status"></div>
    <div id="log-rows"></div>
</div>
</body>
</rml>
)RML";
}
