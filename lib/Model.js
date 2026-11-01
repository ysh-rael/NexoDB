// =============================================================================
// lib/Model.js
// Equivalente ao Model do Mongoose: a classe usada pela aplicacao para
// efetivamente ler e escrever documentos de uma colecao. Cada metodo aqui
// tem a MESMA assinatura do metodo correspondente no Mongoose (ver README,
// secao "metodos do mongoose a replicar"), mas delega a execucao para a
// instancia nativa (Connection -> addon C++).
// =============================================================================
'use strict';

const Query = require('./Query');
const NexoDocument = require('./Document');

function wrap(model, data) {
  return data == null ? null : new NexoDocument(data, model);
}

class Model {
  constructor(name, schema, connection) {
    this.modelName = name;
    this.schema = schema;
    this.connection = connection;
  }

  // lean() - Retorna documentos em forma plana (sem métodos adicionais)
  lean(filter = {}) {
    return new Query((options) =>
      this.connection._native
        .find(filter, options)
        .then((docs) => docs.map((d) => d))
    );
  }

  // ---- Leitura -------------------------------------------------------
  find(filter = {}) {
    return new Query((options) =>
      this.connection._native
        .find(filter, options)
        .then((docs) => docs.map((d) => wrap(this, d)))
    );
  }

  findOne(filter = {}) {
    return new Query((options) =>
      this.connection._native
        .findOne(filter, options)
        .then((doc) => wrap(this, doc))
    );
  }

  findById(id) {
    return new Query((options) =>
      this.connection._native
        .findById(id, options)
        .then((doc) => wrap(this, doc))
    );
  }

  count(filter = {}) {
    return this.connection._native.count(filter);
  }

  countDocuments(filter = {}) {
    return this.connection._native.countDocuments(filter);
  }

  // ---- Atualizacao -----------------------------------------------------
  findOneAndUpdate(filter, update, options = {}) {
    return this.connection._native
      .findOneAndUpdate(filter, update, options)
      .then((doc) => wrap(this, doc));
  }

  findByIdAndUpdate(id, update, options = {}) {
    return this.connection._native
      .findByIdAndUpdate(id, update, options)
      .then((doc) => wrap(this, doc));
  }

  updateOne(filter, update) {
    return this.connection._native.updateOne(filter, update);
  }

  updateMany(filter, update) {
    return this.connection._native.updateMany(filter, update);
  }

  replaceOne(filter, replacement) {
    return this.connection._native.replaceOne(filter, replacement);
  }

  // ---- Remocao -----------------------------------------------------
  deleteOne(filter) {
    return this.connection._native.deleteOne(filter);
  }

  findOneAndRemove(filter) {
    return this.connection._native
      .findOneAndRemove(filter)
      .then((doc) => wrap(this, doc));
  }

  deleteMany(filter) {
    return this.connection._native.deleteMany(filter);
  }

  remove(filter) {
    return this.connection._native.remove(filter);
  }

  findOneAndDelete(filter) {
    return this.connection._native
      .findOneAndDelete(filter)
      .then((doc) => wrap(this, doc));
  }

  findOneAndReplace(filter, replacement) {
    return this.connection._native
      .findOneAndReplace(filter, replacement)
      .then((doc) => wrap(this, doc));
  }

  // ---- Agregacao -----------------------------------------------------
  aggregate(pipeline = []) {
    return this.connection._native.aggregate(pipeline);
  }

  // ---- Escrita simples -------------------------------------------------
  async create(data) {
    const doc = await this.connection._native.insertOne(data);
    return wrap(this, doc);
  }
}

module.exports = Model;
