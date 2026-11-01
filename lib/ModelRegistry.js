// =============================================================================
// lib/ModelRegistry.js
// Registro de modelos por conexao, espelhando mongoose.model()/
// mongoose.models: permite recuperar um Model ja definido por nome em
// qualquer parte da aplicacao, sem precisar reimportar o schema.
// Suporta multiplas conexoes: modelos sao armazenados por (connection, name).
// =============================================================================
'use strict';

const Model = require('./Model');

// Mapa: connection -> Map<name, Model>
const registry = new WeakMap();

/**
 * Obtem ou cria o registro para uma conexao especifica.
 */
function getConnectionRegistry(connection) {
  if (!registry.has(connection)) {
    registry.set(connection, new Map());
  }
  return registry.get(connection);
}

/**
 * Cria (ou recupera) um Model associado a uma colecao em uma conexao especifica.
 * @param {string} name - nome da colecao/modelo.
 * @param {import('./Schema')} [schema] - obrigatorio na primeira chamada para esta conexao.
 * @param {import('./Connection')} [connection] - conexao a usar (obrigatorio na primeira chamada).
 */
function model(name, schema, connection) {
  if (!connection) {
    throw new Error(
      `model(): conexao e obrigatoria para o modelo "${name}".`
    );
  }
  const connRegistry = getConnectionRegistry(connection);
  if (connRegistry.has(name)) {
    return connRegistry.get(name);
  }
  if (!schema) {
    throw new Error(
      `model(): modelo "${name}" ainda nao existe nesta conexao; informe schema na primeira chamada.`
    );
  }
  const m = new Model(name, schema, connection);
  connRegistry.set(name, m);
  return m;
}

// models proxy nao faz sentido com multiplas conexoes, retorna objeto vazio
const models = new Proxy(
  {},
  {
    get: () => undefined,
  }
);

module.exports = { model, models, _registry: registry };
