// =============================================================================
// index.js
// Ponto de entrada publico do pacote "nexodb". Reexporta a API de alto
// nivel (Connection, Schema, Model) que imita a API do Mongoose, mantendo
// o addon nativo (build/Release/nexodb_native.node) como detalhe interno.
// =============================================================================
'use strict';

const Connection = require('./lib/Connection');
const Schema = require('./lib/Schema');
const { model, models } = require('./lib/ModelRegistry');

module.exports = {
  // connect(filePath, options) -> Connection, espelhando mongoose.connect()
  connect(filePath, options = {}) {
    return new Connection({ filePath, ...options });
  },
  Connection,
  Schema,
  model,
  models,
};
