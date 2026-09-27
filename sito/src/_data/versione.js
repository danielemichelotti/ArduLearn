// Versione pubblicata e novità: si leggono dal manifest degli aggiornamenti (lo stesso che usano le schede)
import fs from "node:fs";

export default function () {
  try {
    const m = JSON.parse(fs.readFileSync("../aggiornamenti/manifest.json", "utf-8"));
    return { plc: m.r4wifi?.fw, bridge: m.bridge?.ver, novita: (m.novita || []).slice(0, 5) };
  } catch {
    return { plc: "", bridge: "", novita: [] };
  }
}
