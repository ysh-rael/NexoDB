// =============================================================================
// benchmark/scenarios/bulk-insert.js
// Cenario: inserir N documentos sequencialmente e medir throughput
// (documentos/segundo). Util para avaliar o custo do storage engine e do
// WAL em escritas em lote.
// TODO: habilitar quando Model.create()/insertOne estiver implementado.
// =============================================================================
'use strict';

const DOCUMENT_COUNT = 10_000;

module.exports = {
  name: 'bulk-insert',
  async run() {
    // const conn = require('../../index').connect(tmpFile);
    // for (let i = 0; i < DOCUMENT_COUNT; i++) { await Model.create({...}); }
    return { documentCount: DOCUMENT_COUNT, status: 'TODO: core ainda nao implementado' };
  },
};
