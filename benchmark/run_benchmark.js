// =============================================================================
// benchmark/run_benchmark.js
// Benchmark script for NexoDB - tests performance with specified record count
// Usage: node benchmark/run_benchmark.js [recordCount] [description]
//        node benchmark/run_benchmark.js 1000 1k_registros
//        node benchmark/run_benchmark.js 10000 10k_registros
// =============================================================================
'use strict';

const { connect } = require('../index');
const os = require('node:os');
const path = require('node:path');
const fs = require('node:fs');

const RESULTS_DIR = path.join(__dirname, '..', 'benchmark', 'results');

// Garante que o diretorio de resultados existe
if (!fs.existsSync(RESULTS_DIR)) {
  fs.mkdirSync(RESULTS_DIR, { recursive: true });
}

function generateRandomName() {
  const names = ['Ana', 'Bruno', 'Carlos', 'Diana', 'Eduardo', 'Fernanda', 'Gabriel', 'Helena', 'Igor', 'Julia'];
  const surnames = ['Silva', 'Santos', 'Oliveira', 'Souza', 'Rodrigues', 'Ferreira', 'Alves', 'Pereira', 'Lima', 'Gomes'];
  return `${names[Math.floor(Math.random() * names.length)]} ${surnames[Math.floor(Math.random() * surnames.length)]}`;
}

function generateRandomDoc() {
  return {
    name: generateRandomName(),
    age: Math.floor(Math.random() * 80) + 18,
    email: `user${Math.floor(Math.random() * 10000)}@example.com`,
    active: Math.random() > 0.3,
    score: Math.floor(Math.random() * 1000),
    tags: ['tag1', 'tag2', 'tag3'].slice(0, Math.floor(Math.random() * 3) + 1),
    metadata: {
      createdAt: new Date().toISOString(),
      source: 'benchmark'
    }
  };
}

async function runBenchmark(recordCount, description) {
  const filePath = path.join(os.tmpdir(), `nexodb-bench-${recordCount}-${Date.now()}.nxdb`);
  const conn = connect(filePath, { cacheSizeMb: 256 });

  const { Schema, model } = require('../index');
  const UserSchema = new Schema({
    name: String,
    age: Number,
    email: String,
    active: Boolean,
    score: Number,
    tags: Array,
    metadata: Object
  });
  const User = model('User', UserSchema, conn);

  const results = {
    description,
    recordCount,
    timestamp: new Date().toISOString(),
    operations: {}
  };

  console.log(`\n=== ${description} (${recordCount.toLocaleString()} registros) ===`);

  // --- Benchmark: Insert (create) ---
  console.log('Inserindo documentos...');
  const insertStart = process.hrtime.bigint();
  for (let i = 0; i < recordCount; i++) {
    await User.create(generateRandomDoc());
    if ((i + 1) % 1000 === 0) {
      console.log(`  Inseridos ${i + 1}/${recordCount}`);
    }
  }
  const insertEnd = process.hrtime.bigint();
  const insertTimeMs = Number(insertEnd - insertStart) / 1e6;
  results.operations.insert = {
    timeMs: insertTimeMs,
    opsPerSec: Math.round(recordCount / (insertTimeMs / 1000))
  };
  console.log(`  Tempo: ${insertTimeMs.toFixed(2)}ms (${results.operations.insert.opsPerSec.toLocaleString()} ops/s)`);

  // --- Benchmark: Find all ---
  console.log('Buscando todos os documentos...');
  const findAllStart = process.hrtime.bigint();
  const allDocs = await User.find({}).exec();
  const findAllEnd = process.hrtime.bigint();
  const findAllTimeMs = Number(findAllEnd - findAllStart) / 1e6;
  results.operations.findAll = {
    timeMs: findAllTimeMs,
    opsPerSec: Math.round(recordCount / (findAllTimeMs / 1000)),
    count: allDocs.length
  };
  console.log(`  Tempo: ${findAllTimeMs.toFixed(2)}ms (${results.operations.findAll.opsPerSec.toLocaleString()} ops/s), Encontrados: ${allDocs.length}`);

  // --- Benchmark: Find with filter ---
  console.log('Buscando com filtro (age > 30)...');
  const filterStart = process.hrtime.bigint();
  const filteredDocs = await User.find({ age: { $gt: 30 } }).exec();
  const filterEnd = process.hrtime.bigint();
  const filterTimeMs = Number(filterEnd - filterStart) / 1e6;
  results.operations.findWithFilter = {
    timeMs: filterTimeMs,
    opsPerSec: Math.round(recordCount / (filterTimeMs / 1000)),
    count: filteredDocs.length
  };
  console.log(`  Tempo: ${filterTimeMs.toFixed(2)}ms (${results.operations.findWithFilter.opsPerSec.toLocaleString()} ops/s), Encontrados: ${filteredDocs.length}`);

  // --- Benchmark: Find by ID ---
  console.log('Buscando por ID (100 buscas)...');
  const ids = allDocs.slice(0, 100).map(d => d._id);
  const findByIdStart = process.hrtime.bigint();
  for (const id of ids) {
    await User.findById(id).exec();
  }
  const findByIdEnd = process.hrtime.bigint();
  const findByIdTimeMs = Number(findByIdEnd - findByIdStart) / 1e6;
  results.operations.findById = {
    timeMs: findByIdTimeMs,
    opsPerSec: Math.round(100 / (findByIdTimeMs / 1000))
  };
  console.log(`  Tempo: ${findByIdTimeMs.toFixed(2)}ms (${results.operations.findById.opsPerSec.toLocaleString()} ops/s)`);

  // --- Benchmark: Update ---
  console.log('Atualizando documentos (age > 50)...');
  const updateStart = process.hrtime.bigint();
  const updateResult = await User.updateMany({ age: { $gt: 50 } }, { $set: { active: false } });
  const updateEnd = process.hrtime.bigint();
  const updateTimeMs = Number(updateEnd - updateStart) / 1e6;
  results.operations.updateMany = {
    timeMs: updateTimeMs,
    opsPerSec: Math.round(recordCount / (updateTimeMs / 1000)),
    modifiedCount: updateResult
  };
  console.log(`  Tempo: ${updateTimeMs.toFixed(2)}ms, Modificados: ${updateResult}`);

  // --- Benchmark: Count ---
  console.log('Contando documentos...');
  const countStart = process.hrtime.bigint();
  const count = await User.countDocuments({});
  const countEnd = process.hrtime.bigint();
  const countTimeMs = Number(countEnd - countStart) / 1e6;
  results.operations.count = {
    timeMs: countTimeMs,
    count
  };
  console.log(`  Tempo: ${countTimeMs.toFixed(2)}ms, Total: ${count}`);

  // --- Benchmark: Delete ---
  console.log('Deletando documentos (age < 25)...');
  const deleteStart = process.hrtime.bigint();
  const deleteResult = await User.deleteMany({ age: { $lt: 25 } });
  const deleteEnd = process.hrtime.bigint();
  const deleteTimeMs = Number(deleteEnd - deleteStart) / 1e6;
  results.operations.deleteMany = {
    timeMs: deleteTimeMs,
    opsPerSec: Math.round(recordCount / (deleteTimeMs / 1000)),
    deletedCount: deleteResult
  };
  console.log(`  Tempo: ${deleteTimeMs.toFixed(2)}ms, Deletados: ${deleteResult}`);

  await conn.close();

  // Remove arquivo temporario
  try { fs.unlinkSync(filePath); } catch (e) {}

  return results;
}

async function saveResults(results) {
  const timestamp = new Date().toISOString().replace(/[:.]/g, '-');

  // Salva JSON com timestamp
  const jsonPath = path.join(RESULTS_DIR, `benchmark-${timestamp}.json`);
  fs.writeFileSync(jsonPath, JSON.stringify(results, null, 2));

  // Salva CSV com timestamp
  const csvPath = path.join(RESULTS_DIR, `benchmark-${timestamp}.csv`);
  const csvHeader = 'description,recordCount,operation,timeMs,opsPerSec,count\n';
  let csvContent = csvHeader;

  for (const result of results) {
    for (const [op, data] of Object.entries(result.operations)) {
      csvContent += `${result.description},${result.recordCount},${op},${data.timeMs?.toFixed(2) || ''},${data.opsPerSec || ''},${data.count || data.modifiedCount || data.deletedCount || ''}\n`;
    }
  }

  fs.writeFileSync(csvPath, csvContent);

  // Salva latest-results.json para o dashboard (caminho fixo)
  const latestJsonPath = path.join(__dirname, 'latest-results.json');
  fs.writeFileSync(latestJsonPath, JSON.stringify(results, null, 2));

  // Salva latest-results.csv para o dashboard
  const latestCsvPath = path.join(__dirname, 'latest-results.csv');
  fs.writeFileSync(latestCsvPath, csvContent);

  console.log(`\nResultados salvos em:`);
  console.log(`  JSON: ${jsonPath}`);
  console.log(`  CSV: ${csvPath}`);
  console.log(`  Dashboard JSON: ${latestJsonPath}`);
  console.log(`  Dashboard CSV: ${latestCsvPath}`);

  return { jsonPath, csvPath };
}

async function main() {
  // Parse command line arguments
  const args = process.argv.slice(2);
  const recordCount = parseInt(args[0]) || 1000;
  const description = args[1] || `${recordCount}_registros`;

  console.log('========================================');
  console.log('  NexoDB Benchmark Suite');
  console.log('========================================');

  const results = [await runBenchmark(recordCount, description)];

  // Salva resultados
  await saveResults(results);

  // Sumario
  console.log('\n========================================');
  console.log('  RESUMO DO BENCHMARK');
  console.log('========================================');

  for (const result of results) {
    console.log(`\n${result.description} (${result.recordCount.toLocaleString()} registros):`);
    for (const [op, data] of Object.entries(result.operations)) {
      const ops = data.opsPerSec ? ` (${data.opsPerSec.toLocaleString()} ops/s)` : '';
      const cnt = data.count ? `, count: ${data.count}` : (data.modifiedCount ? `, modified: ${data.modifiedCount}` : (data.deletedCount ? `, deleted: ${data.deletedCount}` : ''));
      console.log(`  ${op}: ${data.timeMs?.toFixed(2) || 'N/A'}ms${ops}${cnt}`);
    }
  }

  console.log('\n========================================');
  console.log('  Benchmark concluido!');
  console.log('========================================');
}

main().catch(err => {
  console.error('Erro no benchmark:', err);
  process.exit(1);
});