// Default V8 GC; input preparation and protocol output are not timed.
const fs = require('node:fs');
const readline = require('node:readline');
const name = process.argv[2];
const legacy = name === 'osu-parser' ? require('osu-parser') : null;
const decoder = name === 'osu-parsers' ? new (require('osu-parsers').BeatmapDecoder)() : null;

function parse(data, path, workload) {
  if (legacy) {
    if (workload === 'file') {
      return new Promise((resolve, reject) =>
        legacy.parseFile(path, (error, map) => error ? reject(error) : resolve(map)));
    }
    return legacy.parseContent(data.toString('utf8'));
  }
  // FOSU does not parse storyboard commands. Keep other sections enabled.
  return workload === 'file'
    ? decoder.decodeFromPath(path, false)
    : decoder.decodeFromBuffer(data, false);
}

(async () => {
  for await (const line of readline.createInterface({input: process.stdin})) {
    const request = JSON.parse(line);
    try {
      const data = fs.readFileSync(request.path);
      let count;
      const ns = [];
      for (let i = 0; i < request.reps; ++i) {
        const start = process.hrtime.bigint();
        let map = request.workload === 'file'
          ? await parse(data, request.path, request.workload)
          : parse(data, request.path, request.workload);
        // Consume the result, but do not force optional geometry/calculations.
        count = map.hitObjects.length;
        map = null;
        ns.push(Number(process.hrtime.bigint() - start));
      }
      console.log(JSON.stringify({count, ns}));
    } catch (error) {
      console.log(JSON.stringify({error: error.name, detail: error.message.slice(0, 240)}));
    }
  }
})();
