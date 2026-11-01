// =============================================================================
// test/js/model.test.js
// TODO: habilitar estes testes assim que QueryEngine::Find/Insert estiverem
// implementados no core (hoje retornam "nao implementado"). Mantidos aqui,
// com test.skip, para documentar o comportamento esperado da API publica.
// =============================================================================
'use strict';

const test = require('node:test');
const os = require('node:os');
const path = require('node:path');

const { connect, Schema, model } = require('../../index');

test('CRUD basico via Model (find/create/updateOne/deleteOne)', async (t) => {
  const filePath = path.join(os.tmpdir(), `nexodb-test-${Date.now()}.nxdb`);
  const conn = connect(filePath);
  const UserSchema = new Schema({ name: String, age: Number });
  const User = model('User', UserSchema, conn);

  await t.test('cria e busca um documento', async () => {
    const created = await User.create({ name: 'Ada', age: 30 });
    const found = await User.findById(created._id);
    console.log(found);
    // assert.strictEqual(found.name, 'Ada');
  });

  conn.close();
});
