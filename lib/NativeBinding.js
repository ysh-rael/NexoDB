// =============================================================================
// lib/NativeBinding.js
// Unico ponto do codigo JS que faz `require` do addon nativo (.node).
// Isola o resto do projeto do caminho de build (Release/Debug) e da
// plataforma, usando o pacote `bindings` para localizar o binario correto.
// =============================================================================
'use strict';

const bindings = require('bindings');

// `bindings('nexodb_native')` procura em build/Release, build/Debug, etc.
// automaticamente, de acordo com a plataforma e o modo de build.
let native = null;

function loadNative() {
  if (!native) {
    native = bindings('nexodb_native');
  }
  return native;
}

module.exports = { loadNative };
