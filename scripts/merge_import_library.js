#!/usr/bin/env node
// Use the application's validation, identity matching, lock, and atomic commit.
const fs = require("node:fs");
const path = require("node:path");
const { spawnSync } = require("node:child_process");
const [source, destination] = process.argv.slice(2);
if (!source || !destination) {
  console.error("Usage: node scripts/merge_import_library.js <import.json|zip> <library.json>");
  process.exit(2);
}
const built = path.resolve(__dirname, "../build/qt6-release/tnuxmusic");
const executable = process.env.TNUXMUSIC_BIN || (fs.existsSync(built) ? built : "tnuxmusic");
const result = spawnSync(executable, ["--merge-library", path.resolve(source), "--library", path.resolve(destination)], { stdio: "inherit" });
if (result.error) console.error(result.error.message);
process.exit(result.status ?? 1);
