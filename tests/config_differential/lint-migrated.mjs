// Runs core's canonical config lint over config/HeadTracking.ini, the committed file, and
// over every distinct file the differential test migrated (the folder it names as the
// argument).
//
// A migrated file may fail two rules. It keeps a player's rebound hotkeys, which the lint
// reports as differing from the fleet's defaults: that rule is for the committed file. And
// it keeps a player's bytes above 0x7F in [Dev] WidgetDumpOuter, the one string row, because
// core's string codec writes a value as stored while its lint holds a canonical file to
// ASCII. That second allowance applies only when every such line is that row; core has to
// settle which of its two rules gives.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { lintCanonicalConfig } from "../../cameraunlock-core/scripts/check-canonical-config.mjs";

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const migratedDir = process.argv[2];
if (!migratedDir) throw new Error("usage: node lint-migrated.mjs <folder of migrated files>");

const options = { dialect: "native", exceptions: undefined };
const REBOUND = /differs from the fleet's /;
const NON_ASCII = /a byte above 0x7F; a canonical file is ASCII only$/;
const failures = [];

const committed = path.join(repo, "config", "HeadTracking.ini");
for (const problem of lintCanonicalConfig(fs.readFileSync(committed), options)) {
  failures.push(`config/HeadTracking.ini: ${problem}`);
}

function nonAsciiOnlyInStringRow(bytes) {
  const lines = bytes.toString("latin1").split("\r\n").filter((l) => /[\x80-\xFF]/.test(l));
  return lines.length > 0 && lines.every((l) => l.startsWith("WidgetDumpOuter="));
}

const files = fs.readdirSync(migratedDir).filter((f) => f.endsWith(".ini"));
if (files.length === 0) throw new Error(`${migratedDir} holds no migrated files`);
let carried = 0;
for (const file of files) {
  const bytes = fs.readFileSync(path.join(migratedDir, file));
  const stringRowOnly = nonAsciiOnlyInStringRow(bytes);
  for (const problem of lintCanonicalConfig(bytes, options)) {
    if (REBOUND.test(problem)) continue;
    if (stringRowOnly && NON_ASCII.test(problem)) {
      carried++;
      continue;
    }
    failures.push(`${file}: ${problem}`);
  }
}

if (failures.length > 0) {
  for (const f of failures) console.log(`FAIL ${f}`);
  process.exit(1);
}
console.log(
  `canonical config lint: the committed file and ${files.length} migrated files pass` +
    ` (${carried} carry a byte above 0x7F in [Dev] WidgetDumpOuter)`,
);
