// =============================================================================
// benchmark/scenarios/concurrent-writes.js
// Cenario: multiplas "conexoes" (workers/threads) escrevendo no mesmo banco
// simultaneamente, para avaliar o comportamento do TransactionManager sob
// contencao (tempo de espera por locks, taxa de conflitos/retries).
// TODO: habilitar quando TransactionManager estiver implementado.
// =============================================================================
'use strict';

module.exports = {
  name: 'concurrent-writes',
  async run() {
    return { status: 'TODO: core ainda nao implementado' };
  },
};
