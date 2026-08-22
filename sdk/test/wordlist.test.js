/* 워드리스트 — 펌웨어와 어긋나면 복구가 통째로 깨진다.
 *   node --test test/wordlist.test.js                                       */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import {
  WORDLIST, wordToIndex, indexToWord, mnemonicToIndices, indicesToMnemonic,
} from '../dist/wordlist.js';
import { WalletError, SW } from '../dist/protocol.js';

test('공식 english.txt 의 SHA-256 과 일치한다', () => {
  // 펌웨어 bip39_wordlist.h 헤더에 적힌 값과 같아야 한다.
  const digest = createHash('sha256').update(WORDLIST.join('\n') + '\n').digest('hex');
  assert.equal(digest, '2f5eed53a4727b4bf8880d8f3f199efc90e58503646d9ff8eff3a2ed3b24dbda');
});

test('2048 단어이고 사전순이다', () => {
  assert.equal(WORDLIST.length, 2048);
  for (let i = 1; i < WORDLIST.length; i++) {
    assert.ok(WORDLIST[i - 1] < WORDLIST[i], `정렬 깨짐: ${WORDLIST[i - 1]} / ${WORDLIST[i]}`);
  }
});

test('알려진 인덱스', () => {
  assert.equal(WORDLIST[0], 'abandon');
  assert.equal(WORDLIST[3], 'about');
  assert.equal(WORDLIST[2047], 'zoo');
  assert.equal(wordToIndex('abandon'), 0);
  assert.equal(wordToIndex('about'), 3);
  assert.equal(wordToIndex('zoo'), 2047);
});

test('앞 4글자만 쳐도 찾아낸다', () => {
  // BIP-39 는 앞 4글자가 유일하도록 설계되어 있다.
  assert.equal(wordToIndex('aban'), wordToIndex('abandon'));
  assert.equal(wordToIndex('ZOO '), 2047);
});

test('없는 단어는 던진다', () => {
  assert.throws(() => wordToIndex('notaword'), WalletError);
  assert.throws(() => wordToIndex(''), WalletError);
  assert.throws(() => indexToWord(2048), (e) => e.status === SW.BAD_PARAM);
});

test('니모닉 왕복', () => {
  const m = 'abandon '.repeat(11) + 'about';
  const idx = mnemonicToIndices(m);
  assert.deepEqual(idx, [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3]);
  assert.equal(indicesToMnemonic(idx), m);
});

test('단어 수가 틀리면 거부한다', () => {
  assert.throws(() => mnemonicToIndices('abandon abandon'), WalletError);
  assert.throws(() => mnemonicToIndices('abandon '.repeat(13)), WalletError);
});

test('공백과 대소문자를 정리한다', () => {
  const m = '  ABANDON\tabandon\n' + 'abandon '.repeat(9) + ' About ';
  assert.deepEqual(mnemonicToIndices(m), [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3]);
});
