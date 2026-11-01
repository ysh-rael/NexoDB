// =============================================================================
// Test findById performance with multiple batches to show index benefit
// =============================================================================
'use strict';

const { connect } = require('../index');
const os = require('node:os');
const path = require('node:path');

async function testFindByIdPerformance() {
  const filePath = path.join(os.tmpdir(), `nexodb-perf-test-${Date.now()}.nxdb`);
  const conn = connect(filePath, { cacheSizeMb: 256 });

  const { Schema, model } = require('../index');
  const UserSchema = new Schema({
    name: String,
    age: Number,
    email: String,
    active: Boolean,
    score: Number,
  });
  const User = model('User', UserSchema, conn);

  const recordCount = 100000;
  console.log(`Inserting ${recordCount} records...`);

  // Insert records
  const insertStart = process.hrtime.bigint();
  for (let i = 0; i < recordCount; i++) {
    await User.create({
      name: `User ${i}`,
      age: Math.floor(Math.random() * 80) + 18,
      email: `user${i}@example.com`,
      active: Math.random() > 0.3,
      score: Math.floor(Math.random() * 1000),
    });
  }
  const insertEnd = process.hrtime.bigint();
  console.log(`Insert time: ${Number(insertEnd - insertStart) / 1e6}ms`);

  // Get all IDs
  const allDocs = await User.find({}).exec();
  const ids = allDocs.map(d => d._id);
  console.log(`Got ${ids.length} IDs`);

  // First batch - includes index build
  console.log('\n--- First batch (includes index build) ---');
  const batch1Start = process.hrtime.bigint();
  for (let i = 0; i < 100; i++) {
    await User.findById(ids[i]).exec();
  }
  const batch1End = process.hrtime.bigint();
  console.log(`Batch 1 (100 lookups): ${Number(batch1End - batch1Start) / 1e6}ms`);

  // Second batch - index already built
  console.log('\n--- Second batch (index already built) ---');
  const batch2Start = process.hrtime.bigint();
  for (let i = 100; i < 200; i++) {
    await User.findById(ids[i]).exec();
  }
  const batch2End = process.hrtime.bigint();
  console.log(`Batch 2 (100 lookups): ${Number(batch2End - batch2Start) / 1e6}ms`);

  // Third batch
  console.log('\n--- Third batch (index already built) ---');
  const batch3Start = process.hrtime.bigint();
  for (let i = 200; i < 300; i++) {
    await User.findById(ids[i]).exec();
  }
  const batch3End = process.hrtime.bigint();
  console.log(`Batch 3 (100 lookups): ${Number(batch3End - batch3Start) / 1e6}ms`);

  // 1000 lookups test
  console.log('\n--- 1000 lookups (index already built) ---');
  const batch4Start = process.hrtime.bigint();
  for (let i = 300; i < 1300; i++) {
    await User.findById(ids[i]).exec();
  }
  const batch4End = process.hrtime.bigint();
  console.log(`1000 lookups: ${Number(batch4End - batch4Start) / 1e6}ms`);

  await conn.close();

  // Cleanup
  const fs = require('node:fs');
  try { fs.unlinkSync(filePath); } catch (e) {}

  console.log('\n=== PERFORMANCE SUMMARY ===');
  console.log('After index is built, findById should be ~0.05-0.1ms per lookup');
}

testFindByIdPerformance().catch(err => {
  console.error('Error:', err);
  process.exit(1);
});