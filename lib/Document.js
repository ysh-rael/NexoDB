// =============================================================================
// lib/Document.js
// Representa um documento individual retornado por uma Query, oferecendo
// metodos de instancia no estilo Mongoose (save, remove, toObject/toJSON).
// E um wrapper fino: o dado real mora no objeto plano `_data` e as
// operacoes de persistencia delegam para o Model associado.
// =============================================================================
'use strict';

class NexoDocument {
  constructor(data, model) {
    this._data = data;
    this._model = model;
    Object.assign(this, data);
  }

  toObject() {
    return { ...this._data };
  }

  toJSON() {
    return this.toObject();
  }

  /** Persiste alteracoes feitas na instancia (equivalente a doc.save()). */
  async save() {
    return this._model.findByIdAndUpdate(this._data._id, this.toObject());
  }

  /** Remove este documento (equivalente a doc.remove()). */
  async remove() {
    return this._model.findByIdAndRemove(this._data._id);
  }
}

module.exports = NexoDocument;
