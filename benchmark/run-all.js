#!/usr/bin/env node
// =============================================================================
// benchmark/run-all.js
// Executa todos os cenarios de benchmark em benchmark/scenarios/*.js,
// coletando tempo de execucao, uso de memoria (RSS/heap) e, quando
// disponivel, I/O de disco, e grava um relatorio JSON em
// benchmark/reports/. Pensado para rodar tanto localmente quanto no CI
// (ver .github/workflows/ci.yml) para detectar regressoes de desempenho.
// =============================================================================
'use strict';

const fs = require('node:fs');
const path = require('node:path');

const SCENARIOS_DIR = path.join(__dirname, 'scenarios');
const REPORTS_DIR = path.join(__dirname, 'reports');

async function runScenario(file) {
  const scenarioPath = path.join(SCENARIOS_DIR, file);
  const scenario = require(scenarioPath);

  const memBefore = process.memoryUsage();
  const start = process.hrtime.bigint();

  const result = await scenario.run();

  const end = process.hrtime.bigint();
  const memAfter = process.memoryUsage();

  return {
    name: scenario.name ?? file,
    durationMs: Number(end - start) / 1e6,
    rssDeltaBytes: memAfter.rss - memBefore.rss,
    heapUsedDeltaBytes: memAfter.heapUsed - memBefore.heapUsed,
    result,
  };
}

async function main() {
  if (!fs.existsSync(SCENARIOS_DIR)) {
    console.error(`Pasta de cenarios nao encontrada: ${SCENARIOS_DIR}`);
    process.exit(1);
  }
  fs.mkdirSync(REPORTS_DIR, { recursive: true });

  const files = fs
    .readdirSync(SCENARIOS_DIR)
    .filter((f) => f.endsWith('.js'));

  const results = [];
  for (const file of files) {
    console.log(`-> Executando cenario: ${file}`);
    try {
      const r = await runScenario(file);
      results.push(r);
      console.log(`   OK (${r.durationMs.toFixed(2)} ms)`);
    } catch (err) {
      console.log(`   FALHOU: ${err.message}`);
      results.push({ name: file, error: err.message });
    }
  }

  const reportPath = path.join(
    REPORTS_DIR,
    `benchmark-${new Date().toISOString().replace(/[:.]/g, '-')}.json`
  );
  fs.writeFileSync(reportPath, JSON.stringify(results, null, 2));
  console.log(`\nRelatorio salvo em: ${reportPath}`);
}

main();
