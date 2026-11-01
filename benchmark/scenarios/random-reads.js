// =============================================================================
// benchmark/scenarios/random-reads.js
// Cenario: leituras aleatorias por _id em uma colecao pre-populada, para
// medir o efeito do cache de paginas (hit/miss) e de indices no tempo de
// resposta sob carga de leitura.
// TODO: habilitar quando findById estiver implementado.
// =============================================================================
'use strict';

module.exports = {
  name: 'random-reads',
  async run() {
    return { status: 'TODO: core ainda nao implementado' };
  },
};
