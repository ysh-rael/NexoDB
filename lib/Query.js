// =============================================================================
// lib/Query.js
// Objeto "thenable" retornado pelos metodos de busca do Model (find,
// findOne, ...), permitindo encadear .sort()/.limit()/.skip()/.select()
// antes de resolver a consulta -- assim como uma Query do Mongoose.
// A consulta so e executada de fato quando o objeto e "await"ado ou
// quando .then()/.exec() e chamado.
// =============================================================================
'use strict';

class Query {
  constructor(executor) {
    // executor: (options) => Promise, implementado pelo Model.
    this._executor = executor;
    this._options = { sort: null, limit: 0, skip: 0, projection: null };
  }

  sort(spec) {
    this._options.sort = spec;
    return this;
  }

  limit(n) {
    this._options.limit = n;
    return this;
  }

  skip(n) {
    this._options.skip = n;
    return this;
  }

  select(projection) {
    this._options.projection = projection;
    return this;
  }

  exec() {
    return this._executor(this._options);
  }

  // retorna documentos em forma plana (sem métodos adicionais)
  lean() {
    return this._executor(this._options).then((docs) => Array.isArray(docs) ? docs.map((d) => d) : docs?._data ?? null);
  }

  then(onFulfilled, onRejected) {
    return this.exec().then(onFulfilled, onRejected);
  }

  catch(onRejected) {
    return this.exec().catch(onRejected);
  }
}

module.exports = Query;
