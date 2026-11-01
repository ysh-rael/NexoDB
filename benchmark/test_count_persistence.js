// =============================================================================
// Test count persistence across database reopen
// =============================================================================
'use strict';

const { connect } = require('../index');
const os = require('node:os');
const path = require('node:path');
const fs = require('node:fs');

async function testCountPersistence() {
  const filePath = path.join(os.tmpdir(), `nexodb-count-test-${Date.now()}.nxdb`);

  // Primeira conexao - insere documentos
  console.log('=== Primeira conexao ===');
  let conn = connect(filePath, { cacheSizeMb: 256 });
  const { Schema, model } = require('../index');
  const UserSchema = new Schema({ name: String, age: Number });
  const User = model('User', UserSchema, conn);

  for (let i = 0; i < 10000; i++) {
    await User.create({ name: `User ${i}`, age: i % 100 });
  }

  let count = await User.countDocuments({});
  console.log(`Count apos inserts: ${count}`);

  // Deleta alguns
  await User.deleteMany({ age: { $lt: 10 } });
  count = await User.countDocuments({});
  console.log(`Count apos deletes: ${count}`);

  await conn.close();

  // Segunda conexao - verifica se count persiste
  console.log('\n=== Segunda conexao (reabre banco) ===');
  conn = connect(filePath, { cacheSizeMb: 256 });
  const User2 = model('User', UserSchema, conn);

  count = await User2.countDocuments({});
  console.log(`Count apos reabrir: ${count}`);

  // Verifica se bate com find().count()
  const all = await User2.find({}).exec();
  console.log(`find().length: ${all.length}`);

  await conn.close();

  // Cleanup
  try { fs.unlinkSync(filePath); } catch (e) {}

  console.log('\n=== Teste de persistencia concluido ===');
}

testCountPersistence().catch(err => {
  console.error('Error:', err);
  process.exit(1);
});