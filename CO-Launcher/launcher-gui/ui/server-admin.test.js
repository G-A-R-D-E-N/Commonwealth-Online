'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const context = vm.createContext({});
vm.runInContext(fs.readFileSync(path.join(__dirname, 'server-admin.js'), 'utf8'), context, { filename: 'server-admin.js' });
const showReports = (data) => vm.runInContext('showReports', context)(data);

test('a report list shows the time in UTC, newest first', () => {
  const text = showReports({
    total: 2,
    reports: [
      { reportId: 'r1', reason: 'cheating', serverTime: 1700000000, reporterPlayerId: 1, reporterName: 'Ann', targetPlayerId: 2, targetName: 'Bob', note: 'speed hack' },
      { reportId: 'r2', reason: 'griefing', serverTime: 1700003600, reporterPlayerId: 3, note: 'walls' },
    ],
  });
  assert.match(text, /^Newest first\./);
  assert.ok(text.indexOf('r2') < text.indexOf('r1'));
  assert.match(text, /2023-11-14 23:13 UTC\s+Griefing\s+r2\s+from #3/);
  assert.match(text, /2023-11-14 22:13 UTC\s+Cheating\s+r1\s+from #1 Ann\s+about #2 Bob\n {4}speed hack/);
});

test('one row with a bad time does not hide the other reports', () => {
  const bad = [
    { reportId: 'text', reason: 'other', serverTime: 'not a time', reporterPlayerId: 1, note: 'a' },
    { reportId: 'huge', reason: 'other', serverTime: 1e300, reporterPlayerId: 2, note: 'b' },
    { reportId: 'nan', reason: 'other', serverTime: Number.NaN, reporterPlayerId: 3, note: 'c' },
    { reportId: 'good', reason: 'support', serverTime: 1700000000, reporterPlayerId: 4, note: 'd' },
  ];
  const text = showReports({ total: bad.length, reports: bad });
  for (const id of ['text', 'huge', 'nan', 'good']) assert.ok(text.includes(id), id);
  assert.equal(text.split('time unknown').length - 1, 3);
  assert.match(text, /2023-11-14 22:13 UTC\s+Support request\s+good/);
});

test('an empty or missing report list gets the empty message', () => {
  assert.equal(showReports({ total: 0, reports: [] }), 'No player reports yet.');
  assert.equal(showReports({}), 'No player reports yet.');
});

test('a long list says how many were shown', () => {
  const reports = Array.from({ length: 3 }, (_, i) => ({ reportId: `r${i}`, reason: 'other', serverTime: 1700000000, reporterPlayerId: 1, note: 'x' }));
  assert.match(showReports({ total: 10, reports }), /^Showing the latest 3 of 10, newest first\./);
});
