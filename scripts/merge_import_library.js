#!/usr/bin/env node
// Merge a tnuxmusic import JSON into a library JSON.
//
// Example:
//   node scripts/merge_import_library.js import.json ~/.local/share/tnux/tnuxmusic/library.json

const fs = require("fs");
const path = require("path");

function usage() {
  console.log("Usage: node scripts/merge_import_library.js <import.json> <library.json>");
}

const [importPathArg, libraryPathArg] = process.argv.slice(2);
if (!importPathArg || !libraryPathArg) {
  usage();
  process.exit(1);
}

const importPath = path.resolve(importPathArg);
const libraryPath = path.resolve(libraryPathArg);
const incoming = JSON.parse(fs.readFileSync(importPath, "utf8"));
const library = fs.existsSync(libraryPath)
  ? JSON.parse(fs.readFileSync(libraryPath, "utf8"))
  : { schema: "tnuxmusic.library.v1", app: "tnuxmusic", version: 1, tracks: [] };

let added = 0;
let replaced = 0;
let replacedByPath = 0;
const incomingByPath = new Map();

function primaryPath(track) {
  return track && track.qualities && track.qualities[0] && track.qualities[0].path
    ? path.resolve(track.qualities[0].path)
    : "";
}

for (const track of incoming.tracks || []) {
  const incomingPath = primaryPath(track);
  if (incomingPath) incomingByPath.set(incomingPath, track.id);
  let pos = library.tracks.findIndex(t => t.id === track.id);
  if (pos < 0 && incomingPath) {
    pos = library.tracks.findIndex(t => primaryPath(t) === incomingPath);
    if (pos >= 0) replacedByPath++;
  }
  if (pos >= 0) {
    library.tracks[pos] = track;
    replaced++;
  } else {
    library.tracks.push(track);
    added++;
  }
}

const beforeDedupe = library.tracks.length;
library.tracks = library.tracks.filter(track => {
  const p = primaryPath(track);
  if (!p || !incomingByPath.has(p)) return true;
  return track.id === incomingByPath.get(p);
});
const dedupedByPath = beforeDedupe - library.tracks.length;

fs.mkdirSync(path.dirname(libraryPath), { recursive: true });
fs.writeFileSync(libraryPath, JSON.stringify(library, null, 4) + "\n", "utf8");

console.log(JSON.stringify({
  importPath,
  libraryPath,
  added,
  replaced,
  replacedByPath,
  dedupedByPath,
  total: library.tracks.length
}, null, 2));
